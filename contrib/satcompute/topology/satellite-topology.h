#ifndef SATCOMPUTE_SATELLITE_TOPOLOGY_H
#define SATCOMPUTE_SATELLITE_TOPOLOGY_H

#include "../routing/size-aware-flow-registry.h"
#include "link/satellite-link-state.h"
#include "snapshot/snapshot-types.h"

#include "ns3/callback.h"
#include "ns3/ipv4-address.h"
#include "ns3/node-container.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ns3 {

struct TopologyConfig
{
  std::string snapshotDirectory;
  double simulationDurationSeconds;
  std::string routingMode;
  uint64_t ecmpHashSeed;
  uint16_t islMtuBytes;
  uint32_t islQueueBytes;
  bool collectQueueDrops;
  bool logEnabled;
};

class SatelliteTopology
{
public:
  explicit SatelliteTopology(const TopologyConfig& config);

  void Initialize();
  void RegisterRouteUpdateCallback(Callback<void> callback);

  uint32_t GetNodeCount() const;
  Ptr<Node> GetNode(uint32_t index) const;
  Ipv4Address GetServiceAddress(uint32_t index) const;
  bool HasSatelliteId(uint32_t satelliteId) const;
  uint32_t GetNodeIndexBySatelliteId(uint32_t satelliteId) const;
  uint32_t GetSatelliteIdByNodeIndex(uint32_t index) const;
  Ptr<Node> GetNodeBySatelliteId(uint32_t satelliteId) const;
  Ipv4Address GetServiceAddressBySatelliteId(uint32_t satelliteId) const;
  std::vector<uint32_t> GetEcmpCandidateSatelliteIds(
    uint32_t sourceSatelliteId,
    uint32_t destinationSatelliteId) const;
  std::vector<EcmpRouteCandidate> GetEcmpRouteCandidates(
    uint32_t sourceSatelliteId,
    uint32_t destinationSatelliteId) const;
  uint32_t GetNextHopSatelliteId(
    uint32_t sourceSatelliteId,
    uint32_t outputInterface) const;
  uint64_t GetIslDataRateBps(uint32_t sourceSatelliteId,
                             uint32_t outputInterface) const;
  uint64_t GetRouteEpoch(uint32_t satelliteId) const;
  uint64_t GetEcmpHashSeed() const;
  bool IsCapacityAwareRouting() const;
  const std::vector<IslDirectedLink>& GetIslDirectedLinks() const;
  const std::vector<IslQueueDropEvent>& GetIslQueueDropEvents() const;
  Ptr<SizeAwareFlowRegistry> GetSizeAwareFlowRegistry() const;

private:
  void CreateSatelliteNodes(const std::vector<uint32_t>& satelliteIds);
  void AssignServiceAddresses();
  void ValidateSatelliteSet(const SatelliteSnapshot& snapshot,
                            const std::string& filename) const;
  void ApplyScheduledSnapshot(std::string nodesFilename,
                              std::string linksFilename);
  void LogSnapshot(const std::string& label,
                   const SatelliteSnapshot& snapshot,
                   const TopologyLinkUpdateSummary& summary) const;

  TopologyConfig m_config;
  NodeContainer m_nodes;
  std::vector<uint32_t> m_satelliteIds;
  std::vector<Ipv4Address> m_serviceAddresses;
  std::map<uint32_t, uint32_t> m_nodeIndexes;
  std::unique_ptr<SatelliteLinkState> m_linkState;
  Ptr<SizeAwareFlowRegistry> m_sizeAwareFlowRegistry;
  std::vector<Callback<void>> m_routeUpdateCallbacks;
};

} // namespace ns3

#endif
