/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_LINK_STATE_H
#define SATCOMPUTE_SATELLITE_LINK_STATE_H

#include "../satellite-id-map.h"
#include "../snapshot/snapshot-types.h"

#include "ns3/net-device-container.h"
#include "ns3/packet.h"

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ns3
{

class SatelliteLinkStateError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct IslDirectedLink
{
    uint32_t sourceSatelliteId{};
    uint32_t destinationSatelliteId{};
    uint32_t outputInterface{};

    bool operator==(const IslDirectedLink&) const = default;
};

struct IslQueueDropEvent
{
    int64_t simulationTimeNs{};
    uint32_t sourceSatelliteId{};
    uint32_t destinationSatelliteId{};
    uint32_t outputInterface{};
    uint32_t packetSizeBytes{};
    uint64_t cumulativeDropPackets{};
    uint64_t cumulativeDropBytes{};
};

struct TopologyLinkUpdateSummary
{
    uint32_t desiredLinks{};
    uint32_t addedLinks{};
    uint32_t reenabledLinks{};
    uint32_t retainedLinks{};
    uint32_t reconfiguredLinks{};
    uint32_t disabledLinks{};

    /** True only when the effective active-edge set changed. */
    bool ActiveEdgeSetChanged() const;
};

/**
 * Own PointToPoint ISLs and atomically apply complete active-link snapshots.
 *
 * Logical input validation completes before device state is mutated. Existing
 * devices and /30 addresses are retained while links are disabled, so a later
 * reactivation preserves interface identity. Attribute-only changes are
 * reported separately and do not count as active-edge changes.
 */
class SatelliteLinkState
{
  public:
    SatelliteLinkState(const SatelliteIdMap& idMap,
                       uint16_t islMtuBytes,
                       uint32_t islQueueBytes,
                       bool collectQueueDrops);

    /** Install the fixed candidate-device superset in an initially down state. */
    void PrepareCandidateLinks(const std::vector<SatelliteLink>& links);
    TopologyLinkUpdateSummary ApplyFullSnapshot(const std::vector<SatelliteLink>& links);
    bool IsLinkActive(uint32_t firstSatelliteId, uint32_t secondSatelliteId) const;
    NetDeviceContainer GetLinkDevices(uint32_t firstSatelliteId,
                                      uint32_t secondSatelliteId) const;
    std::vector<std::pair<uint32_t, uint32_t>> GetActiveLinks() const;
    const std::vector<IslDirectedLink>& GetDirectedLinks() const;
    const std::vector<IslQueueDropEvent>& GetQueueDropEvents() const;

  private:
    using LinkKey = std::pair<uint32_t, uint32_t>;
    using DirectedQueueKey = std::pair<uint32_t, uint32_t>;

    LinkKey MakeKey(uint32_t firstSatelliteId, uint32_t secondSatelliteId) const;
    std::vector<SatelliteLink> ValidateAndCanonicalize(
        const std::vector<SatelliteLink>& links) const;
    NetDeviceContainer InstallLink(const SatelliteLink& link);
    void ConnectQueueDropTrace(Ptr<NetDevice> device,
                               uint32_t sourceSatelliteId,
                               uint32_t destinationSatelliteId);
    static void QueueDropCallback(SatelliteLinkState* state,
                                  IslDirectedLink directedLink,
                                  Ptr<const Packet> packet);
    void RecordQueueDrop(uint32_t sourceSatelliteId,
                         uint32_t destinationSatelliteId,
                         uint32_t outputInterface,
                         Ptr<const Packet> packet);
    void ConfigureLink(const NetDeviceContainer& devices, const SatelliteLink& link) const;
    void SetLinkState(const NetDeviceContainer& devices, bool isUp) const;
    void AssignIpv4Addresses(const NetDeviceContainer& devices);

    SatelliteIdMap m_idMap;
    uint16_t m_islMtuBytes;
    uint32_t m_islQueueBytes;
    bool m_collectQueueDrops;
    uint32_t m_nextIpv4Network{};
    std::map<LinkKey, NetDeviceContainer> m_installedLinks;
    std::map<LinkKey, SatelliteLink> m_linkDefinitions;
    std::set<LinkKey> m_activeLinks;
    std::vector<IslDirectedLink> m_directedLinks;
    std::vector<IslQueueDropEvent> m_queueDropEvents;
    std::map<DirectedQueueKey, std::pair<uint64_t, uint64_t>> m_queueDropTotals;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_LINK_STATE_H
