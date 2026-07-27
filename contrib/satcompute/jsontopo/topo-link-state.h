#ifndef SATCOMPUTE_TOPO_LINK_STATE_H
#define SATCOMPUTE_TOPO_LINK_STATE_H

#include "topo-data.h"

#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/packet.h"

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace ns3 {

struct IslDirectedLink
{
  uint32_t sourceNodeId;
  uint32_t destinationNodeId;
  uint32_t outputInterface;

  bool operator!=(const IslDirectedLink& other) const
  {
    return sourceNodeId != other.sourceNodeId
           || destinationNodeId != other.destinationNodeId
           || outputInterface != other.outputInterface;
  }
};

struct IslQueueDropEvent
{
  int64_t simulationTimeNs;
  uint32_t sourceNodeId;
  uint32_t destinationNodeId;
  uint32_t outputInterface;
  uint32_t packetSizeBytes;
  uint64_t cumulativeDropPackets;
  uint64_t cumulativeDropBytes;
};

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
                     uint32_t islQueueBytes,
                     bool collectQueueDrops);

  TopologyLinkUpdateSummary ApplyFullSnapshot(const std::vector<SatelliteLink>& links);
  const std::vector<IslDirectedLink>& GetDirectedLinks() const;
  const std::vector<IslQueueDropEvent>& GetQueueDropEvents() const;

private:
  typedef std::pair<uint32_t, uint32_t> LinkKey;
  typedef std::pair<uint32_t, uint32_t> DirectedQueueKey;

  LinkKey MakeKey(uint32_t sourceId, uint32_t destinationId) const;
  uint32_t ResolveNodeIndex(uint32_t externalId) const;
  NetDeviceContainer InstallLink(const SatelliteLink& link);
  void ConnectQueueDropTrace(Ptr<NetDevice> device,
                             uint32_t sourceNodeId,
                             uint32_t destinationNodeId);
  static void QueueDropCallback(SatelliteLinkState* state,
                                IslDirectedLink directedLink,
                                Ptr<const Packet> packet);
  void RecordQueueDrop(uint32_t sourceNodeId,
                       uint32_t destinationNodeId,
                       uint32_t outputInterface,
                       Ptr<const Packet> packet);
  void ConfigureLink(const NetDeviceContainer& devices, const SatelliteLink& link) const;
  void SetLinkState(const NetDeviceContainer& devices, bool isUp) const;
  void AssignIpv4Addresses(const NetDeviceContainer& devices);

  NodeContainer m_nodes;
  std::map<uint32_t, uint32_t> m_nodeIndexes;
  uint16_t m_islMtuBytes;
  uint32_t m_islQueueBytes;
  bool m_collectQueueDrops;
  uint32_t m_nextIpv4Network;
  std::map<LinkKey, NetDeviceContainer> m_installedLinks;
  std::set<LinkKey> m_activeLinks;
  std::vector<IslDirectedLink> m_directedLinks;
  std::vector<IslQueueDropEvent> m_queueDropEvents;
  std::map<DirectedQueueKey, std::pair<uint64_t, uint64_t>> m_queueDropTotals;
};

} // namespace ns3

#endif
