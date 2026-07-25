#include "topo-link-state.h"

#include "ns3/data-rate.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4.h"
#include "ns3/point-to-point-module.h"
#include "ns3/string.h"

#include <limits>
#include <string>

namespace ns3 {

SatelliteLinkState::SatelliteLinkState(const NodeContainer& nodes,
                                       const std::map<uint32_t, uint32_t>& nodeIndexes)
  : m_nodes(nodes),
    m_nodeIndexes(nodeIndexes),
    m_nextIpv4Network(0)
{
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
      device->SetAttribute("DataRate", dataRate);
    }

  Ptr<PointToPointNetDevice> first =
    DynamicCast<PointToPointNetDevice>(devices.Get(0));
  Ptr<PointToPointChannel> channel =
    DynamicCast<PointToPointChannel>(first->GetChannel());
  NS_ABORT_MSG_IF(channel == nullptr, "星间链路信道不是 PointToPointChannel");
  channel->SetAttribute("Delay", TimeValue(MicroSeconds(link.delayUs)));
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
  helper.SetChannelAttribute("Delay", TimeValue(MicroSeconds(link.delayUs)));
  helper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));

  NetDeviceContainer devices =
    helper.Install(NodeContainer(m_nodes.Get(sourceIndex), m_nodes.Get(destinationIndex)));
  AssignIpv4Addresses(devices);
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

} // namespace ns3
