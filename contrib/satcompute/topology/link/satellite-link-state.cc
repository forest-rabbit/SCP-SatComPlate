// 创建和维护 PointToPoint ISL 的带宽、时延、MTU、队列及启停状态。

#include "satellite-link-state.h"

#include "ns3/data-rate.h"
#include "ns3/callback.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4.h"
#include "ns3/point-to-point-module.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <limits>
#include <string>

namespace ns3 {

SatelliteLinkState::SatelliteLinkState(const NodeContainer& nodes,
                                       const std::map<uint32_t, uint32_t>& nodeIndexes,
                                       uint16_t islMtuBytes,
                                       uint32_t islQueueBytes,
                                       bool collectQueueDrops)
  : m_nodes(nodes),
    m_nodeIndexes(nodeIndexes),
    m_islMtuBytes(islMtuBytes),
    m_islQueueBytes(islQueueBytes),
    m_collectQueueDrops(collectQueueDrops),
    m_nextIpv4Network(0)
{
  NS_ABORT_MSG_IF(m_islMtuBytes < 68,
                  "ISL MTU 必须至少为 68 bytes");
  NS_ABORT_MSG_IF(m_islQueueBytes == 0,
                  "ISL queue 必须至少为 1 byte");
}

SatelliteLinkState::LinkKey
SatelliteLinkState::MakeKey(uint32_t sourceId, uint32_t destinationId) const
{
  return sourceId < destinationId
           ? std::make_pair(sourceId, destinationId)
           : std::make_pair(destinationId, sourceId);
}

uint32_t
SatelliteLinkState::ResolveNodeIndex(uint32_t externalId) const
{
  auto node = m_nodeIndexes.find(externalId);
  NS_ABORT_MSG_IF(node == m_nodeIndexes.end(),
                  "运行期快照引用了初始快照中不存在的卫星: " << externalId);
  return node->second;
}

void
SatelliteLinkState::AssignIpv4Addresses(const NetDeviceContainer& devices)
{
  static const uint32_t networksInTenSlashEight = 1u << 22;
  NS_ABORT_MSG_IF(m_nextIpv4Network >= networksInTenSlashEight,
                  "10.0.0.0/8 中没有剩余的 /30 星间链路网段");

  uint32_t rawAddress = 0x0a000000u + m_nextIpv4Network * 4u;
  ++m_nextIpv4Network;

  Ipv4AddressHelper ipv4;
  ipv4.SetBase(Ipv4Address(rawAddress), Ipv4Mask("255.255.255.252"));
  ipv4.Assign(devices);
}

void
SatelliteLinkState::ConfigureLink(const NetDeviceContainer& devices,
                                  const SatelliteLink& link) const
{
  NS_ABORT_MSG_IF(link.bandwidthBps == 0, "星间链路带宽必须大于 0");
  DataRateValue dataRate{DataRate(link.bandwidthBps)};
  for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
      Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(devices.Get(i));
      NS_ABORT_MSG_IF(device == nullptr, "星间链路设备不是 PointToPointNetDevice");
      NS_ABORT_MSG_IF(!device->SetMtu(m_islMtuBytes),
                      "无法设置星间链路 MTU: " << m_islMtuBytes);
      device->SetAttribute("DataRate", dataRate);
    }

  Ptr<PointToPointNetDevice> first =
    DynamicCast<PointToPointNetDevice>(devices.Get(0));
  Ptr<PointToPointChannel> channel =
    DynamicCast<PointToPointChannel>(first->GetChannel());
  NS_ABORT_MSG_IF(channel == nullptr, "星间链路信道不是 PointToPointChannel");
  channel->SetAttribute("Delay", TimeValue(MicroSeconds(link.delayUs)));
}

void
SatelliteLinkState::ConnectQueueDropTrace(Ptr<NetDevice> netDevice,
                                          uint32_t sourceNodeId,
                                          uint32_t destinationNodeId)
{
  Ptr<PointToPointNetDevice> device =
    DynamicCast<PointToPointNetDevice>(netDevice);
  NS_ABORT_MSG_IF(device == nullptr,
                  "星间链路设备不是 PointToPointNetDevice");
  Ptr<Ipv4> ipv4 = device->GetNode()->GetObject<Ipv4>();
  NS_ABORT_MSG_IF(ipv4 == nullptr, "卫星节点没有 IPv4 协议栈");
  int32_t interface = ipv4->GetInterfaceForDevice(device);
  NS_ABORT_MSG_IF(interface < 0, "星间链路设备没有 IPv4 接口");

  uint32_t outputInterface = static_cast<uint32_t>(interface);
  IslDirectedLink directedLink = {
    sourceNodeId,
    destinationNodeId,
    outputInterface
  };
  m_directedLinks.push_back(directedLink);

  if (!m_collectQueueDrops)
    {
      return;
    }
  Ptr<Queue<Packet>> queue = device->GetQueue();
  NS_ABORT_MSG_IF(queue == nullptr, "星间链路设备没有发送队列");
  bool connected = queue->TraceConnectWithoutContext(
    "Drop",
    MakeBoundCallback(&SatelliteLinkState::QueueDropCallback,
                      this,
                      directedLink));
  NS_ABORT_MSG_IF(!connected,
                  "无法连接 ISL queue Drop trace: "
                    << sourceNodeId << "->" << destinationNodeId
                    << " interface=" << outputInterface);
}

void
SatelliteLinkState::QueueDropCallback(SatelliteLinkState* state,
                                      IslDirectedLink directedLink,
                                      Ptr<const Packet> packet)
{
  state->RecordQueueDrop(directedLink.sourceNodeId,
                         directedLink.destinationNodeId,
                         directedLink.outputInterface,
                         packet);
}

void
SatelliteLinkState::RecordQueueDrop(uint32_t sourceNodeId,
                                    uint32_t destinationNodeId,
                                    uint32_t outputInterface,
                                    Ptr<const Packet> packet)
{
  NS_ABORT_MSG_IF(packet == nullptr, "ISL queue Drop trace 收到空 packet");
  DirectedQueueKey key = std::make_pair(sourceNodeId, outputInterface);
  auto& totals = m_queueDropTotals[key];
  ++totals.first;
  totals.second += packet->GetSize();
  m_queueDropEvents.push_back(
    {Simulator::Now().GetNanoSeconds(),
     sourceNodeId,
     destinationNodeId,
     outputInterface,
     packet->GetSize(),
     totals.first,
     totals.second});
}

NetDeviceContainer
SatelliteLinkState::InstallLink(const SatelliteLink& link)
{
  uint32_t sourceIndex = ResolveNodeIndex(link.sourceId);
  uint32_t destinationIndex = ResolveNodeIndex(link.destinationId);
  NS_ABORT_MSG_IF(link.bandwidthBps == 0, "星间链路带宽必须大于 0");

  PointToPointHelper helper;
  helper.SetDeviceAttribute("DataRate",
                            DataRateValue(DataRate(link.bandwidthBps)));
  helper.SetDeviceAttribute("Mtu", UintegerValue(m_islMtuBytes));
  helper.SetChannelAttribute("Delay", TimeValue(MicroSeconds(link.delayUs)));
  helper.SetQueue("ns3::DropTailQueue",
                  "MaxSize",
                  StringValue(std::to_string(m_islQueueBytes) + "B"));

  NetDeviceContainer devices =
    helper.Install(NodeContainer(m_nodes.Get(sourceIndex), m_nodes.Get(destinationIndex)));
  AssignIpv4Addresses(devices);
  ConnectQueueDropTrace(devices.Get(0), link.sourceId, link.destinationId);
  ConnectQueueDropTrace(devices.Get(1), link.destinationId, link.sourceId);
  return devices;
}

void
SatelliteLinkState::SetLinkState(const NetDeviceContainer& devices, bool isUp) const
{
  for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
      Ptr<NetDevice> device = devices.Get(i);
      Ptr<Ipv4> ipv4 = device->GetNode()->GetObject<Ipv4>();
      NS_ABORT_MSG_IF(ipv4 == nullptr, "卫星节点没有 IPv4 协议栈");
      int32_t interface = ipv4->GetInterfaceForDevice(device);
      NS_ABORT_MSG_IF(interface < 0, "星间链路设备没有 IPv4 接口");

      if (isUp)
        {
          ipv4->SetUp(static_cast<uint32_t>(interface));
        }
      else
        {
          ipv4->SetDown(static_cast<uint32_t>(interface));
        }
    }
}

TopologyLinkUpdateSummary
SatelliteLinkState::ApplyFullSnapshot(const std::vector<SatelliteLink>& links)
{
  TopologyLinkUpdateSummary summary;
  summary.desiredLinks = links.size();
  std::set<LinkKey> desiredLinks;

  for (const auto& link : links)
    {
      ResolveNodeIndex(link.sourceId);
      ResolveNodeIndex(link.destinationId);
      LinkKey key = MakeKey(link.sourceId, link.destinationId);
      NS_ABORT_MSG_IF(!desiredLinks.insert(key).second,
                      "快照包含重复卫星链路: " << key.first << "<->" << key.second);

      auto installed = m_installedLinks.find(key);
      if (installed == m_installedLinks.end())
        {
          NetDeviceContainer devices = InstallLink(link);
          m_installedLinks.insert(std::make_pair(key, devices));
          m_activeLinks.insert(key);
          ++summary.addedLinks;
          continue;
        }

      ConfigureLink(installed->second, link);
      if (m_activeLinks.find(key) == m_activeLinks.end())
        {
          SetLinkState(installed->second, true);
          m_activeLinks.insert(key);
          ++summary.reenabledLinks;
        }
      else
        {
          ++summary.unchangedLinks;
        }
    }

  std::vector<LinkKey> linksToDisable;
  for (const auto& active : m_activeLinks)
    {
      if (desiredLinks.find(active) == desiredLinks.end())
        {
          linksToDisable.push_back(active);
        }
    }

  for (const auto& key : linksToDisable)
    {
      SetLinkState(m_installedLinks.at(key), false);
      m_activeLinks.erase(key);
      ++summary.disabledLinks;
    }

  return summary;
}

const std::vector<IslDirectedLink>&
SatelliteLinkState::GetDirectedLinks() const
{
  return m_directedLinks;
}

const std::vector<IslQueueDropEvent>&
SatelliteLinkState::GetQueueDropEvents() const
{
  return m_queueDropEvents;
}

} // namespace ns3
