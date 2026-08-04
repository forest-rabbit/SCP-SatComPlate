/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_REPLAY_TOPOLOGY_CONTROLLER_H
#define SATCOMPUTE_REPLAY_TOPOLOGY_CONTROLLER_H

#include "../../model/scenario-config.h"
#include "../../routing/algorithm/capacity-aware-path-view.h"
#include "../../routing/common/routing-mode.h"
#include "../../routing/state/flow-route-registry.h"
#include "../ipv4/satellite-ipv4-addressing.h"
#include "../link/satellite-link-state.h"
#include "../satellite-id-map.h"
#include "../snapshot/snapshot-types.h"

#include "ns3/callback.h"
#include "ns3/ipv4-address.h"
#include "ns3/node-container.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ReplayTopologyControllerError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Build and schedule a deterministic IPv4 topology from JSON replay slices.
 *
 * The controller installs the SatCompute ns-3.48 global-routing adapter and
 * must outlive all scheduled simulation events.
 */
class ReplayTopologyController : public CapacityAwarePathView
{
  public:
    explicit ReplayTopologyController(const ScenarioConfig& config);

    void Initialize();
    void RegisterRouteUpdateCallback(Callback<void> callback);
    void InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const;

    const ScenarioConfig& GetConfig() const;
    const NodeContainer& GetNodes() const;
    const SatelliteIdMap& GetIdMap() const;
    const SatelliteLinkState& GetLinkState() const;
    Ptr<FlowRouteRegistry> GetFlowRouteRegistry() const;
    Ipv4Address GetServiceAddress(uint32_t satelliteId) const;
    std::vector<EcmpRouteCandidate> GetEcmpRouteCandidates(
        uint32_t sourceSatelliteId,
        uint32_t destinationSatelliteId) const override;
    uint32_t GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                   uint32_t outputInterface) const override;
    uint64_t GetIslDataRateBps(uint32_t sourceSatelliteId,
                               uint32_t outputInterface) const override;
    uint64_t GetRouteEpoch(uint32_t satelliteId) const;
    uint64_t GetHashSeed() const;
    bool IsCapacityAwareRouting() const;
    uint32_t GetAppliedSnapshotCount() const;
    uint32_t GetRouteComputationCount() const;
    const TopologyLinkUpdateSummary& GetLastUpdateSummary() const;

  private:
    SatelliteSnapshot ReadAndNormalizeSnapshot(
        const std::filesystem::path& nodesFilename,
        const std::filesystem::path& linksFilename) const;
    void ValidateSatelliteIds(const SatelliteSnapshot& snapshot,
                              const std::filesystem::path& filename) const;
    void ApplyScheduledSnapshot(std::filesystem::path nodesFilename,
                                std::filesystem::path linksFilename);
    void RequireInitialized() const;

    ScenarioConfig m_config;
    RoutingMode m_routingMode{RoutingMode::GLOBAL_FIRST};
    NodeContainer m_nodes;
    std::unique_ptr<SatelliteIdMap> m_idMap;
    std::unique_ptr<SatelliteIpv4ServiceMap> m_serviceMap;
    std::unique_ptr<SatelliteLinkState> m_linkState;
    Ptr<FlowRouteRegistry> m_flowRouteRegistry;
    std::vector<Callback<void>> m_routeUpdateCallbacks;
    std::vector<uint32_t> m_expectedSatelliteIds;
    TopologyLinkUpdateSummary m_lastUpdateSummary;
    uint32_t m_appliedSnapshotCount{};
    uint32_t m_routeComputationCount{};
    bool m_initialized{};
};

} // namespace ns3

#endif // SATCOMPUTE_REPLAY_TOPOLOGY_CONTROLLER_H
