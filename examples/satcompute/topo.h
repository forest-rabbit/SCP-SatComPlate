#ifndef SATCOMPUTE_TOPO_H
#define SATCOMPUTE_TOPO_H

#include "jsontopo/topo-data.h"
#include "jsontopo/topo-link-state.h"

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
  uint64_t defaultLinkBandwidthBps;
};

class SatelliteTopology
{
public:
  explicit SatelliteTopology(const TopologyConfig& config);

  void Initialize();

  uint32_t GetNodeCount() const;
  Ptr<Node> GetNode(uint32_t index) const;
  Ipv4Address GetServiceAddress(uint32_t index) const;

private:
  void CreateSatelliteNodes(const std::vector<uint32_t>& satelliteIds);
  void AssignServiceAddresses();
  void ValidateSatelliteSet(const SatelliteSnapshot& snapshot,
                            const std::string& filename) const;
  void ApplyScheduledSnapshot(std::string filename);
  void LogSnapshot(const std::string& label,
                   const SatelliteSnapshot& snapshot,
                   const TopologyLinkUpdateSummary& summary) const;

  TopologyConfig m_config;
  NodeContainer m_nodes;
  std::vector<uint32_t> m_satelliteIds;
  std::vector<Ipv4Address> m_serviceAddresses;
  std::map<uint32_t, uint32_t> m_nodeIndexes;
  std::unique_ptr<SatelliteLinkState> m_linkState;
};

} // namespace ns3

#endif
