/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_TOPOLOGY_H
#define SATCOMPUTE_SATELLITE_TOPOLOGY_H

#include "link/satellite-link-state.h"
#include "satellite-id-map.h"
#include "satellite-topology-controller.h"

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace ns3
{

class OnlineOrbitConstellation;
class OnlineTopologyController;
struct ConstellationDefinition;
struct SatComputeConfig;

class SatelliteTopologyError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Legacy-compatible public topology facade backed by ns-3.48 online orbit state.
 *
 * The facade is the only platform-level topology entry. It delegates lifecycle,
 * routing, endpoint, link-state, and run-counter services to the online
 * controller. Pre-generated topology slices are not a simulation input.
 */
class SatelliteTopology : public SatelliteTopologyController
{
  public:
    SatelliteTopology(const SatComputeConfig& config,
                      const ConstellationDefinition& constellation);
    ~SatelliteTopology() override;

    void Initialize() override;
    void RegisterRouteUpdateCallback(Callback<void> callback) override;
    void InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const override;

    const NodeContainer& GetNodes() const override;
    const SatelliteIdMap& GetIdMap() const override;
    const SatelliteLinkState& GetLinkState() const override;
    bool ApplyCommunicationFaultOverlay(
        const std::set<uint32_t>& unavailableSatelliteIds,
        bool refreshNaturalState) override;
    const OnlineOrbitConstellation& GetOnlineConstellation() const;

    uint32_t GetNodeCount() const;
    Ptr<Node> GetNode(uint32_t index) const;
    Ipv4Address GetServiceAddress(uint32_t index) const;
    bool HasSatelliteId(uint32_t satelliteId) const override;
    uint32_t GetNodeIndexBySatelliteId(uint32_t satelliteId) const;
    uint32_t GetSatelliteIdByNodeIndex(uint32_t index) const;
    Ptr<Node> GetNodeBySatelliteId(uint32_t satelliteId) const override;
    Ipv4Address GetServiceAddressBySatelliteId(uint32_t satelliteId) const override;

    std::vector<uint32_t> GetEcmpCandidateSatelliteIds(
        uint32_t sourceSatelliteId,
        uint32_t destinationSatelliteId) const;
    std::vector<EcmpRouteCandidate> GetEcmpRouteCandidates(
        uint32_t sourceSatelliteId,
        uint32_t destinationSatelliteId) const override;
    uint32_t GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                   uint32_t outputInterface) const override;
    uint64_t GetIslDataRateBps(uint32_t sourceSatelliteId,
                               uint32_t outputInterface) const override;
    uint64_t GetRouteEpoch(uint32_t satelliteId) const override;
    uint64_t GetEcmpHashSeed() const;
    uint64_t GetHashSeed() const override;
    bool IsCapacityAwareRouting() const override;
    const std::vector<IslDirectedLink>& GetIslDirectedLinks() const;
    const std::vector<IslQueueDropEvent>& GetIslQueueDropEvents() const;
    Ptr<FlowRouteRegistry> GetFlowRouteRegistry() const override;
    uint32_t GetAppliedTopologySliceCount() const override;
    uint32_t GetRouteComputationCount() const override;

  private:
    std::unique_ptr<OnlineTopologyController> m_controller;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_TOPOLOGY_H
