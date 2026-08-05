/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "online-topology-controller.h"

#include "../../routing/ns3/satcompute-ipv4-global-routing-helper.h"

#include "ns3/data-rate.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <string>

namespace ns3
{

OnlineTopologyController::OnlineTopologyController(const ResolvedSatComputeConfig& config)
    : m_config(config),
      m_topologyPolicy(config.constellation,
                       config.network.seamEnabled,
                       config.network.maxIslDistanceM,
                       config.network.delayMode,
                       config.network.fixedDelayNs)
{
    if (m_config.network.topologySource != "online" ||
        m_config.network.replayDirectory)
    {
        throw OnlineTopologyControllerError(
            "online controller requires an online source without a replay directory");
    }
    if (!TryParseRoutingMode(m_config.routing.mode, m_routingMode))
    {
        throw OnlineTopologyControllerError("unknown routing mode " + m_config.routing.mode);
    }
    if (m_routingMode != RoutingMode::GLOBAL_FIRST &&
        m_routingMode != RoutingMode::HASH_PER_FLOW &&
        m_routingMode != RoutingMode::HRW_PER_FLOW &&
        m_routingMode != RoutingMode::SIZE_AWARE_HRW &&
        m_routingMode != RoutingMode::CAPACITY_AWARE_HRW)
    {
        throw OnlineTopologyControllerError("unsupported online routing mode");
    }
    if (m_config.network.networkUpdateIntervalNs <= 0 ||
        m_config.simulation.durationNs <= 0)
    {
        throw OnlineTopologyControllerError(
            "online duration and network update interval must be positive");
    }
    const uint64_t scheduledUpdateCount = static_cast<uint64_t>(
        (m_config.simulation.durationNs - 1) /
        m_config.network.networkUpdateIntervalNs);
    if (scheduledUpdateCount >= std::numeric_limits<uint32_t>::max())
    {
        throw OnlineTopologyControllerError(
            "online topology update count exceeds uint32 range");
    }
}

void
OnlineTopologyController::Initialize()
{
    if (m_initialized)
    {
        throw OnlineTopologyControllerError(
            "online topology controller was initialized more than once");
    }
    if (!Simulator::Now().IsZero())
    {
        throw OnlineTopologyControllerError(
            "online topology controller must be initialized at simulation time zero");
    }

    m_constellation = std::make_unique<OnlineOrbitConstellation>(m_config.constellation);
    const NodeContainer& nodes = m_constellation->GetNodes();
    const SatelliteIdMap& idMap = m_constellation->GetIdMap();
    if (IsReservationAwareRoutingMode(m_routingMode))
    {
        m_flowRouteRegistry = CreateObject<FlowRouteRegistry>();
    }

    Ipv4StaticRoutingHelper staticRouting;
    SatComputeIpv4GlobalRoutingHelper globalRouting(m_routingMode,
                                                    m_config.routing.hashSeed,
                                                    m_flowRouteRegistry);
    Ipv4ListRoutingHelper listRouting;
    listRouting.Add(staticRouting, 0);
    listRouting.Add(globalRouting, -10);
    InternetStackHelper internet;
    internet.SetRoutingHelper(listRouting);
    internet.Install(nodes);
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        SatComputeIpv4GlobalRoutingHelper::GetRouting(nodes.Get(index))
            ->SetSatelliteId(idMap.GetSatelliteIdByNodeIndex(index));
    }

    m_serviceMap = std::make_unique<SatelliteIpv4ServiceMap>(idMap);
    m_linkState = std::make_unique<SatelliteLinkState>(idMap,
                                                       m_config.network.islMtuBytes,
                                                       m_config.network.islQueueBytes,
                                                       m_config.logging.diagnosticMode ==
                                                           "failure");
    m_lastTopologyState = m_topologyPolicy.EvaluateCurrent(*m_constellation);
    m_linkState->PrepareCandidateLinks(
        m_lastTopologyState.GetCandidateLinks(m_config.network.linkBandwidthBps));
    m_lastUpdateSummary = m_linkState->ApplyFullSnapshot(
        m_lastTopologyState.GetActiveLinks(m_config.network.linkBandwidthBps));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    m_appliedUpdateCount = 1;
    m_routeComputationCount = 1;
    m_appliedUpdateTimesNs.push_back(0);
    m_initialized = true;

    const int64_t intervalNs = m_config.network.networkUpdateIntervalNs;
    const int64_t durationNs = m_config.simulation.durationNs;
    for (int64_t updateTimeNs = intervalNs; updateTimeNs < durationNs;)
    {
        Simulator::Schedule(NanoSeconds(updateTimeNs),
                            &OnlineTopologyController::ApplyScheduledUpdate,
                            this);
        if (durationNs - updateTimeNs <= intervalNs)
        {
            break;
        }
        updateTimeNs += intervalNs;
    }
}

void
OnlineTopologyController::ApplyScheduledUpdate()
{
    m_lastTopologyState = m_topologyPolicy.EvaluateCurrent(*m_constellation);
    m_lastUpdateSummary = m_linkState->ApplyFullSnapshot(
        m_lastTopologyState.GetActiveLinks(m_config.network.linkBandwidthBps));
    ++m_appliedUpdateCount;
    m_appliedUpdateTimesNs.push_back(Simulator::Now().GetNanoSeconds());
    if (m_lastUpdateSummary.ActiveEdgeSetChanged())
    {
        Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
        SatComputeIpv4GlobalRoutingHelper::AdvanceRouteEpoch(GetNodes());
        ++m_routeComputationCount;
        for (const Callback<void>& callback : m_routeUpdateCallbacks)
        {
            callback();
        }
    }
}

void
OnlineTopologyController::RegisterRouteUpdateCallback(Callback<void> callback)
{
    if (callback.IsNull())
    {
        throw OnlineTopologyControllerError("route update callback cannot be null");
    }
    m_routeUpdateCallbacks.push_back(callback);
}

void
OnlineTopologyController::InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const
{
    RequireInitialized();
    SatComputeIpv4GlobalRoutingHelper::InvalidateDecisionCache(GetNodes(), flowKey);
}

void
OnlineTopologyController::RequireInitialized() const
{
    if (!m_initialized)
    {
        throw OnlineTopologyControllerError("online topology controller is not initialized");
    }
}

const ResolvedSatComputeConfig&
OnlineTopologyController::GetConfig() const
{
    return m_config;
}

const NodeContainer&
OnlineTopologyController::GetNodes() const
{
    RequireInitialized();
    return m_constellation->GetNodes();
}

const SatelliteIdMap&
OnlineTopologyController::GetIdMap() const
{
    RequireInitialized();
    return m_constellation->GetIdMap();
}

const SatelliteLinkState&
OnlineTopologyController::GetLinkState() const
{
    RequireInitialized();
    return *m_linkState;
}

const OnlineOrbitConstellation&
OnlineTopologyController::GetConstellation() const
{
    RequireInitialized();
    return *m_constellation;
}

const CircularOrbitTopologyState&
OnlineTopologyController::GetLastTopologyState() const
{
    RequireInitialized();
    return m_lastTopologyState;
}

Ptr<FlowRouteRegistry>
OnlineTopologyController::GetFlowRouteRegistry() const
{
    RequireInitialized();
    return m_flowRouteRegistry;
}

Ptr<Node>
OnlineTopologyController::GetNodeBySatelliteId(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_constellation->GetIdMap().GetNodeBySatelliteId(satelliteId);
}

bool
OnlineTopologyController::HasSatelliteId(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_constellation->GetIdMap().HasSatelliteId(satelliteId);
}

Ipv4Address
OnlineTopologyController::GetServiceAddress(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_serviceMap->GetServiceAddress(satelliteId);
}

Ipv4Address
OnlineTopologyController::GetServiceAddressBySatelliteId(uint32_t satelliteId) const
{
    return GetServiceAddress(satelliteId);
}

std::vector<EcmpRouteCandidate>
OnlineTopologyController::GetEcmpRouteCandidates(uint32_t sourceSatelliteId,
                                                 uint32_t destinationSatelliteId) const
{
    RequireInitialized();
    if (sourceSatelliteId == destinationSatelliteId)
    {
        throw OnlineTopologyControllerError(
            "ECMP candidate query requires different satellites");
    }
    Ptr<SatComputeIpv4GlobalRouting> routing = SatComputeIpv4GlobalRoutingHelper::GetRouting(
        m_constellation->GetIdMap().GetNodeBySatelliteId(sourceSatelliteId));
    return routing->GetEffectiveRouteCandidates(GetServiceAddress(destinationSatelliteId));
}

uint32_t
OnlineTopologyController::GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                                uint32_t outputInterface) const
{
    RequireInitialized();
    const std::vector<IslDirectedLink>& links = m_linkState->GetDirectedLinks();
    const auto link = std::find_if(
        links.begin(),
        links.end(),
        [sourceSatelliteId, outputInterface](const IslDirectedLink& item) {
            return item.sourceSatelliteId == sourceSatelliteId &&
                   item.outputInterface == outputInterface;
        });
    if (link == links.end())
    {
        throw OnlineTopologyControllerError(
            "output interface cannot be mapped to an ISL next hop");
    }
    return link->destinationSatelliteId;
}

uint64_t
OnlineTopologyController::GetIslDataRateBps(uint32_t sourceSatelliteId,
                                            uint32_t outputInterface) const
{
    RequireInitialized();
    Ptr<Node> source = m_constellation->GetIdMap().GetNodeBySatelliteId(sourceSatelliteId);
    Ptr<Ipv4> ipv4 = source->GetObject<Ipv4>();
    if (ipv4 == nullptr || outputInterface >= ipv4->GetNInterfaces())
    {
        throw OnlineTopologyControllerError("ISL data-rate query has an invalid interface");
    }
    Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(ipv4->GetNetDevice(outputInterface));
    if (device == nullptr)
    {
        throw OnlineTopologyControllerError(
            "ISL data-rate query target is not point-to-point");
    }
    DataRateValue dataRate;
    if (!device->GetAttributeFailSafe("DataRate", dataRate) ||
        dataRate.Get().GetBitRate() == 0)
    {
        throw OnlineTopologyControllerError("ISL data-rate query returned zero bandwidth");
    }
    return dataRate.Get().GetBitRate();
}

uint64_t
OnlineTopologyController::GetRouteEpoch(uint32_t satelliteId) const
{
    RequireInitialized();
    return SatComputeIpv4GlobalRoutingHelper::GetRouting(
               m_constellation->GetIdMap().GetNodeBySatelliteId(satelliteId))
        ->GetRouteEpoch();
}

uint64_t
OnlineTopologyController::GetHashSeed() const
{
    return m_config.routing.hashSeed;
}

bool
OnlineTopologyController::IsCapacityAwareRouting() const
{
    return m_routingMode == RoutingMode::CAPACITY_AWARE_HRW;
}

uint32_t
OnlineTopologyController::GetAppliedUpdateCount() const
{
    RequireInitialized();
    return m_appliedUpdateCount;
}

uint32_t
OnlineTopologyController::GetAppliedTopologySliceCount() const
{
    return GetAppliedUpdateCount();
}

uint32_t
OnlineTopologyController::GetRouteComputationCount() const
{
    RequireInitialized();
    return m_routeComputationCount;
}

const std::vector<int64_t>&
OnlineTopologyController::GetAppliedUpdateTimesNs() const
{
    RequireInitialized();
    return m_appliedUpdateTimesNs;
}

const TopologyLinkUpdateSummary&
OnlineTopologyController::GetLastUpdateSummary() const
{
    RequireInitialized();
    return m_lastUpdateSummary;
}

} // namespace ns3
