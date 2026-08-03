// 创建卫星节点与 ISL，并按 JSON 快照更新链路和重算全局路由。

#include "satellite-topology.h"

#include "../routing/ns3/satcompute-ipv4-global-routing-helper.h"
#include "snapshot/snapshot-reader.h"
#include "snapshot/snapshot-schedule.h"

#include "ns3/abort.h"
#include "ns3/csma-net-device.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/internet-module.h"
#include "ns3/mac48-address.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <iostream>

namespace ns3 {

SatelliteTopology::SatelliteTopology(const TopologyConfig& config)
  : m_config(config),
    m_routingMode(RoutingMode::GLOBAL_FIRST)
{
  NS_ABORT_MSG_IF(config.snapshotDirectory.empty(), "topologyDir 不能为空");
  NS_ABORT_MSG_IF(config.simulationDurationSeconds <= 0.0,
                  "simulationDuration 必须大于 0");
  NS_ABORT_MSG_IF(!TryParseRoutingMode(config.routingMode, m_routingMode),
                  "未知 routingMode: " << config.routingMode);
  NS_ABORT_MSG_IF(config.islMtuBytes < 68,
                  "islMtuBytes 必须至少为 68");
  NS_ABORT_MSG_IF(config.islQueueBytes == 0,
                  "islQueueBytes 必须大于 0");
  if (IsReservationAwareRoutingMode(m_routingMode))
    {
      m_flowRouteRegistry = CreateObject<FlowRouteRegistry>();
    }
}

void
SatelliteTopology::CreateSatelliteNodes(const std::vector<uint32_t>& satelliteIds)
{
  m_satelliteIds = satelliteIds;
  m_nodes.Create(satelliteIds.size());
  for (uint32_t index = 0; index < satelliteIds.size(); ++index)
    {
      m_nodeIndexes[satelliteIds[index]] = index;
    }

  Ipv4StaticRoutingHelper staticRouting;
  SatComputeIpv4GlobalRoutingHelper globalRouting(
    m_routingMode,
    m_config.ecmpHashSeed,
    m_flowRouteRegistry);
  Ipv4ListRoutingHelper listRouting;
  listRouting.Add(staticRouting, 0);
  listRouting.Add(globalRouting, -10);

  InternetStackHelper internet;
  internet.SetRoutingHelper(listRouting);
  internet.Install(m_nodes);
  for (uint32_t index = 0; index < m_nodes.GetN(); ++index)
    {
      SatComputeIpv4GlobalRoutingHelper::GetRouting(m_nodes.Get(index))
        ->SetSatelliteId(m_satelliteIds[index]);
    }
  AssignServiceAddresses();
}

void
SatelliteTopology::AssignServiceAddresses()
{
  static const uint32_t firstServiceAddress = 0xac100001u;
  static const uint32_t serviceAddressCount = 1u << 20;
  NS_ABORT_MSG_IF(m_nodes.GetN() >= serviceAddressCount - 1,
                  "172.16.0.0/12 服务地址空间不足");

  m_serviceAddresses.reserve(m_nodes.GetN());
  for (uint32_t index = 0; index < m_nodes.GetN(); ++index)
    {
      Ptr<Node> node = m_nodes.Get(index);
      Ptr<CsmaNetDevice> device = CreateObject<CsmaNetDevice>();
      device->SetAddress(Mac48Address::Allocate());
      device->SetQueue(CreateObject<DropTailQueue<Packet>>());
      node->AddDevice(device);

      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
      int32_t interface = ipv4->AddInterface(device);
      Ipv4Address address(firstServiceAddress + index);
      ipv4->AddAddress(interface,
                       Ipv4InterfaceAddress(address, Ipv4Mask("255.255.255.255")));
      ipv4->SetMetric(interface, 1);
      ipv4->SetUp(interface);
      m_serviceAddresses.push_back(address);
    }
}

void
SatelliteTopology::ValidateSatelliteSet(const SatelliteSnapshot& snapshot,
                                        const std::string& filename) const
{
  NS_ABORT_MSG_IF(snapshot.satelliteIds != m_satelliteIds,
                  "运行期快照的卫星集合与初始快照不一致: " << filename);
}

void
SatelliteTopology::LogSnapshot(const std::string& label,
                               const SatelliteSnapshot& snapshot,
                               const TopologyLinkUpdateSummary& summary) const
{
  if (!m_config.logEnabled)
    {
      return;
    }
  std::cout << "[TOPO:" << label << "] @ "
            << Simulator::Now().GetSeconds() << "s" << std::endl
            << "  satellites : " << snapshot.satelliteIds.size() << std::endl
            << "  ISLs       : " << summary.desiredLinks << std::endl
            << "  added      : " << summary.addedLinks << std::endl
            << "  restored   : " << summary.reenabledLinks << std::endl
            << "  unchanged  : " << summary.unchangedLinks << std::endl
            << "  disabled   : " << summary.disabledLinks << std::endl
            << std::endl;
}

void
SatelliteTopology::ApplyScheduledSnapshot(std::string nodesFilename,
                                          std::string linksFilename)
{
  SatelliteSnapshot snapshot =
    ReadSatelliteSnapshot(nodesFilename, linksFilename);
  ValidateSatelliteSet(snapshot, nodesFilename);
  TopologyLinkUpdateSummary summary = m_linkState->ApplyFullSnapshot(snapshot.links);

  // Use the stock ns-3 global route manager after the complete link snapshot
  // has been applied, so each time slice triggers exactly one recomputation.
  Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
  SatComputeIpv4GlobalRoutingHelper::AdvanceRouteEpoch(m_nodes);
  for (const auto& callback : m_routeUpdateCallbacks)
    {
      callback();
    }
  LogSnapshot("Update", snapshot, summary);
}

void
SatelliteTopology::RegisterRouteUpdateCallback(Callback<void> callback)
{
  NS_ABORT_MSG_IF(callback.IsNull(),
                  "route update callback 不能为空");
  m_routeUpdateCallbacks.push_back(callback);
}

void
SatelliteTopology::InvalidateFlowRouteDecisionCache(
  const EcmpFlowKey& flowKey) const
{
  SatComputeIpv4GlobalRoutingHelper::InvalidateDecisionCache(m_nodes,
                                                             flowKey);
}

void
SatelliteTopology::Initialize()
{
  SnapshotSchedule schedule =
    ScanSatelliteSnapshots(m_config.snapshotDirectory,
                           m_config.simulationDurationSeconds);
  SatelliteSnapshot initial =
    ReadSatelliteSnapshot(schedule.initialNodesFilename,
                          schedule.initialLinksFilename);
  NS_ABORT_MSG_IF(initial.links.empty(), "初始快照中没有星间链路");

  CreateSatelliteNodes(initial.satelliteIds);
  m_linkState.reset(
    new SatelliteLinkState(m_nodes,
                           m_nodeIndexes,
                           m_config.islMtuBytes,
                           m_config.islQueueBytes,
                           m_config.collectQueueDrops));
  TopologyLinkUpdateSummary summary = m_linkState->ApplyFullSnapshot(initial.links);
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  if (m_config.logEnabled)
    {
      std::cout << "[TOPO:Plan]" << std::endl
                << "  directory  : " << m_config.snapshotDirectory << std::endl
                << "  nodes      : " << schedule.initialNodesFilename << std::endl
                << "  topology   : " << schedule.initialLinksFilename << std::endl
                << "  discovered : " << schedule.discoveredSnapshotCount << std::endl
                << "  selected   : " << schedule.selectedSnapshotCount << std::endl
                << "  updates    : " << schedule.updates.size() << std::endl
                << std::endl;
    }
  LogSnapshot("Initial", initial, summary);

  for (const auto& update : schedule.updates)
    {
      Simulator::Schedule(Seconds(update.timeSeconds),
                          &SatelliteTopology::ApplyScheduledSnapshot,
                          this,
                          update.nodesFilename,
                          update.linksFilename);
    }
}

uint32_t
SatelliteTopology::GetNodeCount() const
{
  return m_nodes.GetN();
}

Ptr<Node>
SatelliteTopology::GetNode(uint32_t index) const
{
  NS_ABORT_MSG_IF(index >= m_nodes.GetN(), "卫星节点下标越界: " << index);
  return m_nodes.Get(index);
}

Ipv4Address
SatelliteTopology::GetServiceAddress(uint32_t index) const
{
  NS_ABORT_MSG_IF(index >= m_serviceAddresses.size(), "卫星业务地址下标越界: " << index);
  return m_serviceAddresses[index];
}

std::vector<uint32_t>
SatelliteTopology::GetEcmpCandidateSatelliteIds(
  uint32_t sourceSatelliteId,
  uint32_t destinationSatelliteId) const
{
  NS_ABORT_MSG_IF(sourceSatelliteId == destinationSatelliteId,
                  "ECMP candidate audit 不接受相同源和目的卫星");
  std::vector<EcmpRouteCandidate> routes =
    GetEcmpRouteCandidates(sourceSatelliteId, destinationSatelliteId);
  std::vector<uint32_t> candidateSatelliteIds;
  const std::vector<IslDirectedLink>& directedLinks =
    m_linkState->GetDirectedLinks();
  for (const auto& route : routes)
    {
      auto directed =
        std::find_if(
          directedLinks.begin(),
          directedLinks.end(),
          [sourceSatelliteId, &route](const IslDirectedLink& link) {
            return link.sourceNodeId == sourceSatelliteId
                   && link.outputInterface == route.outputInterface;
          });
      NS_ABORT_MSG_IF(
        directed == directedLinks.end(),
        "ECMP host route 无法映射到物理下一跳: source="
          << sourceSatelliteId
          << " interface=" << route.outputInterface);
      candidateSatelliteIds.push_back(directed->destinationNodeId);
    }
  std::sort(candidateSatelliteIds.begin(), candidateSatelliteIds.end());
  candidateSatelliteIds.erase(
    std::unique(candidateSatelliteIds.begin(),
                candidateSatelliteIds.end()),
    candidateSatelliteIds.end());
  return candidateSatelliteIds;
}

std::vector<EcmpRouteCandidate>
SatelliteTopology::GetEcmpRouteCandidates(
  uint32_t sourceSatelliteId,
  uint32_t destinationSatelliteId) const
{
  NS_ABORT_MSG_IF(sourceSatelliteId == destinationSatelliteId,
                  "ECMP candidate 查询不接受相同源和目的卫星");
  Ptr<SatComputeIpv4GlobalRouting> routing =
    SatComputeIpv4GlobalRoutingHelper::GetRouting(
      GetNodeBySatelliteId(sourceSatelliteId));
  return routing->GetEffectiveRouteCandidates(
    GetServiceAddressBySatelliteId(destinationSatelliteId));
}

uint32_t
SatelliteTopology::GetNextHopSatelliteId(
  uint32_t sourceSatelliteId,
  uint32_t outputInterface) const
{
  const std::vector<IslDirectedLink>& directedLinks =
    m_linkState->GetDirectedLinks();
  auto directed =
    std::find_if(
      directedLinks.begin(),
      directedLinks.end(),
      [sourceSatelliteId, outputInterface](const IslDirectedLink& link) {
        return link.sourceNodeId == sourceSatelliteId
               && link.outputInterface == outputInterface;
      });
  NS_ABORT_MSG_IF(
    directed == directedLinks.end(),
    "output interface 无法映射到物理下一跳: source="
      << sourceSatelliteId << " interface=" << outputInterface);
  return directed->destinationNodeId;
}

uint64_t
SatelliteTopology::GetIslDataRateBps(uint32_t sourceSatelliteId,
                                     uint32_t outputInterface) const
{
  Ptr<Node> source = GetNodeBySatelliteId(sourceSatelliteId);
  Ptr<Ipv4> ipv4 = source->GetObject<Ipv4>();
  NS_ABORT_MSG_IF(ipv4 == nullptr
                    || outputInterface >= ipv4->GetNInterfaces(),
                  "ISL data-rate 查询的接口无效: source="
                    << sourceSatelliteId
                    << " interface=" << outputInterface);
  Ptr<PointToPointNetDevice> device =
    DynamicCast<PointToPointNetDevice>(
      ipv4->GetNetDevice(outputInterface));
  NS_ABORT_MSG_IF(device == nullptr,
                  "ISL data-rate 查询目标不是 PointToPointNetDevice");
  DataRateValue dataRate;
  NS_ABORT_MSG_IF(!device->GetAttributeFailSafe("DataRate", dataRate)
                    || dataRate.Get().GetBitRate() == 0,
                  "ISL data-rate 查询得到零带宽");
  return dataRate.Get().GetBitRate();
}

uint64_t
SatelliteTopology::GetRouteEpoch(uint32_t satelliteId) const
{
  return SatComputeIpv4GlobalRoutingHelper::GetRouting(
           GetNodeBySatelliteId(satelliteId))
    ->GetRouteEpoch();
}

uint64_t
SatelliteTopology::GetEcmpHashSeed() const
{
  return m_config.ecmpHashSeed;
}

bool
SatelliteTopology::IsCapacityAwareRouting() const
{
  return m_routingMode == RoutingMode::CAPACITY_AWARE_HRW;
}

bool
SatelliteTopology::HasSatelliteId(uint32_t satelliteId) const
{
  return m_nodeIndexes.find(satelliteId) != m_nodeIndexes.end();
}

uint32_t
SatelliteTopology::GetNodeIndexBySatelliteId(uint32_t satelliteId) const
{
  auto node = m_nodeIndexes.find(satelliteId);
  NS_ABORT_MSG_IF(node == m_nodeIndexes.end(), "未知的卫星 ID: " << satelliteId);
  return node->second;
}

uint32_t
SatelliteTopology::GetSatelliteIdByNodeIndex(uint32_t index) const
{
  NS_ABORT_MSG_IF(index >= m_satelliteIds.size(), "卫星节点下标越界: " << index);
  return m_satelliteIds[index];
}

Ptr<Node>
SatelliteTopology::GetNodeBySatelliteId(uint32_t satelliteId) const
{
  return GetNode(GetNodeIndexBySatelliteId(satelliteId));
}

Ipv4Address
SatelliteTopology::GetServiceAddressBySatelliteId(uint32_t satelliteId) const
{
  return GetServiceAddress(GetNodeIndexBySatelliteId(satelliteId));
}

const std::vector<IslDirectedLink>&
SatelliteTopology::GetIslDirectedLinks() const
{
  NS_ABORT_MSG_IF(m_linkState == nullptr, "卫星拓扑尚未初始化");
  return m_linkState->GetDirectedLinks();
}

const std::vector<IslQueueDropEvent>&
SatelliteTopology::GetIslQueueDropEvents() const
{
  NS_ABORT_MSG_IF(m_linkState == nullptr, "卫星拓扑尚未初始化");
  return m_linkState->GetQueueDropEvents();
}

Ptr<FlowRouteRegistry>
SatelliteTopology::GetFlowRouteRegistry() const
{
  return m_flowRouteRegistry;
}

} // namespace ns3
