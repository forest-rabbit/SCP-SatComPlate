/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ONLINE_TOPOLOGY_CONTROLLER_H
#define SATCOMPUTE_ONLINE_TOPOLOGY_CONTROLLER_H

#include "../../model/scenario-config.h"
#include "../../routing/common/routing-mode.h"
#include "../../routing/state/flow-route-registry.h"
#include "../ipv4/satellite-ipv4-addressing.h"
#include "../link/satellite-link-state.h"
#include "../orbit/online-orbit-constellation.h"
#include "../satellite-runtime-view.h"
#include "circular-orbit-topology-policy.h"

#include "ns3/callback.h"
#include "ns3/ipv4-address.h"
#include "ns3/node-container.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ns3
{

class OnlineTopologyControllerError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** Build and periodically update an IPv4 topology from online orbit state. */
class OnlineTopologyController : public SatelliteRuntimeView
{
  public:
    explicit OnlineTopologyController(const ScenarioConfig& config);

    void Initialize();
    void RegisterRouteUpdateCallback(Callback<void> callback) override;
    void InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const override;

    const ScenarioConfig& GetConfig() const;
    const NodeContainer& GetNodes() const;
    const SatelliteIdMap& GetIdMap() const;
    const SatelliteLinkState& GetLinkState() const;
    const OnlineOrbitConstellation& GetConstellation() const;
    const CircularOrbitTopologyState& GetLastTopologyState() const;
    Ptr<Node> GetNodeBySatelliteId(uint32_t satelliteId) const override;
    Ptr<FlowRouteRegistry> GetFlowRouteRegistry() const override;
    bool HasSatelliteId(uint32_t satelliteId) const override;
    Ipv4Address GetServiceAddress(uint32_t satelliteId) const override;
    std::vector<EcmpRouteCandidate> GetEcmpRouteCandidates(
        uint32_t sourceSatelliteId,
        uint32_t destinationSatelliteId) const override;
    uint32_t GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                   uint32_t outputInterface) const override;
    uint64_t GetIslDataRateBps(uint32_t sourceSatelliteId,
                               uint32_t outputInterface) const override;
    uint64_t GetRouteEpoch(uint32_t satelliteId) const override;
    uint64_t GetHashSeed() const override;
    bool IsCapacityAwareRouting() const override;
    uint32_t GetAppliedUpdateCount() const;
    uint32_t GetRouteComputationCount() const;
    const std::vector<int64_t>& GetAppliedUpdateTimesNs() const;
    const TopologyLinkUpdateSummary& GetLastUpdateSummary() const;

  private:
    void ApplyScheduledUpdate();
    void RequireInitialized() const;

    ScenarioConfig m_config;
    RoutingMode m_routingMode{RoutingMode::GLOBAL_FIRST};
    CircularOrbitTopologyPolicy m_topologyPolicy;
    std::unique_ptr<OnlineOrbitConstellation> m_constellation;
    std::unique_ptr<SatelliteIpv4ServiceMap> m_serviceMap;
    std::unique_ptr<SatelliteLinkState> m_linkState;
    Ptr<FlowRouteRegistry> m_flowRouteRegistry;
    std::vector<Callback<void>> m_routeUpdateCallbacks;
    CircularOrbitTopologyState m_lastTopologyState;
    TopologyLinkUpdateSummary m_lastUpdateSummary;
    std::vector<int64_t> m_appliedUpdateTimesNs;
    uint32_t m_appliedUpdateCount{};
    uint32_t m_routeComputationCount{};
    bool m_initialized{};
};

} // namespace ns3

#endif // SATCOMPUTE_ONLINE_TOPOLOGY_CONTROLLER_H
