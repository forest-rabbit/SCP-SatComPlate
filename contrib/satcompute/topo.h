#ifndef SATCOMPUTE_TOPO_H
#define SATCOMPUTE_TOPO_H

#include "jsontopo/topo-link-state.h"
#include "routing/size-aware-flow-registry.h"
#include "topology/snapshot/snapshot-types.h"

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

  uint32_t GetNodeCount() const;
  Ptr<Node> GetNode(uint32_t index) const;
  Ipv4Address GetServiceAddress(uint32_t index) const;
  bool HasSatelliteId(uint32_t satelliteId) const;
  uint32_t GetNodeIndexBySatelliteId(uint32_t satelliteId) const;
  uint32_t GetSatelliteIdByNodeIndex(uint32_t index) const;
  Ptr<Node> GetNodeBySatelliteId(uint32_t satelliteId) const;
  Ipv4Address GetServiceAddressBySatelliteId(uint32_t satelliteId) const;
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
};

} // namespace ns3

#endif
