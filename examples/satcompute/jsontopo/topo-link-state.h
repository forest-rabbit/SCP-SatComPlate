#ifndef SATCOMPUTE_TOPO_LINK_STATE_H
#define SATCOMPUTE_TOPO_LINK_STATE_H

#include "topo-data.h"

#include "ns3/net-device-container.h"
#include "ns3/node-container.h"

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace ns3 {

struct TopologyLinkUpdateSummary
{
  uint32_t desiredLinks;
  uint32_t addedLinks;
  uint32_t reenabledLinks;
  uint32_t unchangedLinks;
  uint32_t disabledLinks;

  TopologyLinkUpdateSummary()
    : desiredLinks(0),
      addedLinks(0),
      reenabledLinks(0),
      unchangedLinks(0),
      disabledLinks(0)
  {
  }
};

class SatelliteLinkState
{
public:
  SatelliteLinkState(const NodeContainer& nodes,
                     const std::map<uint32_t, uint32_t>& nodeIndexes,
                     uint16_t islMtuBytes,
                     uint32_t islQueueBytes);

  TopologyLinkUpdateSummary ApplyFullSnapshot(const std::vector<SatelliteLink>& links);

private:
  typedef std::pair<uint32_t, uint32_t> LinkKey;

  LinkKey MakeKey(uint32_t sourceId, uint32_t destinationId) const;
  uint32_t ResolveNodeIndex(uint32_t externalId) const;
  NetDeviceContainer InstallLink(const SatelliteLink& link);
  void ConfigureLink(const NetDeviceContainer& devices, const SatelliteLink& link) const;
  void SetLinkState(const NetDeviceContainer& devices, bool isUp) const;
  void AssignIpv4Addresses(const NetDeviceContainer& devices);

  NodeContainer m_nodes;
  std::map<uint32_t, uint32_t> m_nodeIndexes;
  uint16_t m_islMtuBytes;
  uint32_t m_islQueueBytes;
  uint32_t m_nextIpv4Network;
  std::map<LinkKey, NetDeviceContainer> m_installedLinks;
  std::set<LinkKey> m_activeLinks;
};

} // namespace ns3

#endif
