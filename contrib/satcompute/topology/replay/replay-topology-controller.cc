/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "replay-topology-controller.h"

#include "../snapshot/snapshot-reader.h"
#include "../snapshot/snapshot-schedule.h"
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
#include <map>
#include <numeric>
#include <string>

namespace ns3
{

ReplayTopologyController::ReplayTopologyController(const ScenarioConfig& config)
    : m_config(config)
{
    if (m_config.constellation.orbitProvider != "json-replay" ||
        m_config.network.topologySource != "json-replay" ||
        !m_config.network.replayDirectory)
    {
        throw ReplayTopologyControllerError(
            "replay controller requires matching json-replay orbit and topology sources");
    }
    if (!TryParseRoutingMode(m_config.routing.mode, m_routingMode))
    {
        throw ReplayTopologyControllerError("unknown routing mode " +
                                            m_config.routing.mode);
    }
    if (m_routingMode != RoutingMode::GLOBAL_FIRST &&
        m_routingMode != RoutingMode::HASH_PER_FLOW &&
        m_routingMode != RoutingMode::HRW_PER_FLOW &&
        m_routingMode != RoutingMode::SIZE_AWARE_HRW &&
        m_routingMode != RoutingMode::CAPACITY_AWARE_HRW)
    {
        throw ReplayTopologyControllerError(
            "unsupported replay routing mode");
    }
    if (m_config.network.delayMode == "fixed" &&
        !m_config.network.fixedDelayNs)
    {
        throw ReplayTopologyControllerError(
            "fixed replay requires a resolved fixed delay");
    }
}

void
ReplayTopologyController::ValidateSatelliteIds(
    const SatelliteSnapshot& snapshot,
    const std::filesystem::path& filename) const
{
    if (snapshot.satelliteIds != m_expectedSatelliteIds)
    {
        throw ReplayTopologyControllerError(
            "replay satellite ID set differs from the scenario at " +
            filename.string());
    }
}

SatelliteSnapshot
ReplayTopologyController::ReadAndNormalizeSnapshot(
    const std::filesystem::path& nodesFilename,
    const std::filesystem::path& linksFilename,
    int64_t expectedTimeNs) const
{
    SatelliteSnapshot snapshot =
        ReadSatelliteSnapshot(nodesFilename, linksFilename, expectedTimeNs);
    ValidateSatelliteIds(snapshot, nodesFilename);
    for (SatelliteLink& link : snapshot.links)
    {
        link.bandwidthBps = m_config.network.linkBandwidthBps;
        if (m_config.network.delayMode == "fixed")
        {
            link.delayNs = *m_config.network.fixedDelayNs;
        }
    }
    return snapshot;
}

void
ReplayTopologyController::Initialize()
{
    if (m_initialized)
    {
        throw ReplayTopologyControllerError(
            "replay topology controller was initialized more than once");
    }

    const SnapshotSchedule schedule = ScanSatelliteSnapshots(
        *m_config.network.replayDirectory,
        m_config.simulation.durationNs,
        m_config.network.networkUpdateIntervalNs);
    const SatelliteSnapshot rawInitial = ReadSatelliteSnapshot(
        schedule.initialNodesFilename,
        schedule.initialLinksFilename,
        0);

    const uint32_t satelliteCount = m_config.constellation.GetSatelliteCount();
    m_expectedSatelliteIds.resize(satelliteCount);
    std::iota(m_expectedSatelliteIds.begin(), m_expectedSatelliteIds.end(), 0);
    ValidateSatelliteIds(rawInitial, schedule.initialNodesFilename);

    m_nodes.Create(satelliteCount);
    m_idMap =
        std::make_unique<SatelliteIdMap>(m_nodes, m_expectedSatelliteIds);
    if (IsReservationAwareRoutingMode(m_routingMode))
    {
        m_flowRouteRegistry = CreateObject<FlowRouteRegistry>();
    }
    Ipv4StaticRoutingHelper staticRouting;
    SatComputeIpv4GlobalRoutingHelper globalRouting(
        m_routingMode,
        m_config.routing.hashSeed,
        m_flowRouteRegistry);
    Ipv4ListRoutingHelper listRouting;
    listRouting.Add(staticRouting, 0);
    listRouting.Add(globalRouting, -10);

    InternetStackHelper internet;
    internet.SetRoutingHelper(listRouting);
    internet.Install(m_nodes);
    for (uint32_t index = 0; index < m_nodes.GetN(); ++index)
    {
        SatComputeIpv4GlobalRoutingHelper::GetRouting(m_nodes.Get(index))
            ->SetSatelliteId(m_expectedSatelliteIds[index]);
    }
    m_serviceMap = std::make_unique<SatelliteIpv4ServiceMap>(*m_idMap);
    m_linkState = std::make_unique<SatelliteLinkState>(
        *m_idMap,
        m_config.network.islMtuBytes,
        m_config.network.islQueueBytes,
        false);

    const SatelliteSnapshot initial = ReadAndNormalizeSnapshot(
        schedule.initialNodesFilename,
        schedule.initialLinksFilename,
        0);
    std::map<std::pair<uint32_t, uint32_t>, SatelliteLink> candidateLinks;
    const auto rememberCandidates = [&candidateLinks](const SatelliteSnapshot& snapshot) {
        for (const SatelliteLink& link : snapshot.links)
        {
            candidateLinks.try_emplace(
                std::make_pair(link.sourceId, link.destinationId),
                link);
        }
    };
    rememberCandidates(initial);
    for (const SnapshotUpdate& update : schedule.updates)
    {
        rememberCandidates(ReadAndNormalizeSnapshot(update.nodesFilename,
                                                    update.linksFilename,
                                                    update.timeNs));
    }
    std::vector<SatelliteLink> candidateDefinitions;
    candidateDefinitions.reserve(candidateLinks.size());
    for (const auto& item : candidateLinks)
    {
        candidateDefinitions.push_back(item.second);
    }
    m_linkState->PrepareCandidateLinks(candidateDefinitions);
    m_lastUpdateSummary = m_linkState->ApplyFullSnapshot(initial.links);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    m_appliedSnapshotCount = 1;
    m_routeComputationCount = 1;
    m_initialized = true;

    for (const SnapshotUpdate& update : schedule.updates)
    {
        Simulator::Schedule(NanoSeconds(update.timeNs),
                            &ReplayTopologyController::ApplyScheduledSnapshot,
                            this,
                            update.timeNs,
                            update.nodesFilename,
                            update.linksFilename);
    }
}

void
ReplayTopologyController::ApplyScheduledSnapshot(
    int64_t expectedTimeNs,
    std::filesystem::path nodesFilename,
    std::filesystem::path linksFilename)
{
    const SatelliteSnapshot snapshot =
        ReadAndNormalizeSnapshot(nodesFilename, linksFilename, expectedTimeNs);
    m_lastUpdateSummary = m_linkState->ApplyFullSnapshot(snapshot.links);
    ++m_appliedSnapshotCount;
    if (m_lastUpdateSummary.ActiveEdgeSetChanged())
    {
        Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
        SatComputeIpv4GlobalRoutingHelper::AdvanceRouteEpoch(m_nodes);
        ++m_routeComputationCount;
        for (const Callback<void>& callback : m_routeUpdateCallbacks)
        {
            callback();
        }
    }
}

void
ReplayTopologyController::RegisterRouteUpdateCallback(Callback<void> callback)
{
    if (callback.IsNull())
    {
        throw ReplayTopologyControllerError("route update callback cannot be null");
    }
    m_routeUpdateCallbacks.push_back(callback);
}

void
ReplayTopologyController::InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const
{
    RequireInitialized();
    SatComputeIpv4GlobalRoutingHelper::InvalidateDecisionCache(m_nodes, flowKey);
}

void
ReplayTopologyController::RequireInitialized() const
{
    if (!m_initialized)
    {
        throw ReplayTopologyControllerError(
            "replay topology controller is not initialized");
    }
}

const ScenarioConfig&
ReplayTopologyController::GetConfig() const
{
    return m_config;
}

const NodeContainer&
ReplayTopologyController::GetNodes() const
{
    RequireInitialized();
    return m_nodes;
}

const SatelliteIdMap&
ReplayTopologyController::GetIdMap() const
{
    RequireInitialized();
    return *m_idMap;
}

const SatelliteLinkState&
ReplayTopologyController::GetLinkState() const
{
    RequireInitialized();
    return *m_linkState;
}

Ptr<FlowRouteRegistry>
ReplayTopologyController::GetFlowRouteRegistry() const
{
    RequireInitialized();
    return m_flowRouteRegistry;
}

Ptr<Node>
ReplayTopologyController::GetNodeBySatelliteId(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_idMap->GetNodeBySatelliteId(satelliteId);
}

bool
ReplayTopologyController::HasSatelliteId(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_idMap->HasSatelliteId(satelliteId);
}

Ipv4Address
ReplayTopologyController::GetServiceAddress(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_serviceMap->GetServiceAddress(satelliteId);
}

std::vector<EcmpRouteCandidate>
ReplayTopologyController::GetEcmpRouteCandidates(uint32_t sourceSatelliteId,
                                                 uint32_t destinationSatelliteId) const
{
    RequireInitialized();
    if (sourceSatelliteId == destinationSatelliteId)
    {
        throw ReplayTopologyControllerError(
            "ECMP candidate query requires different satellites");
    }
    Ptr<SatComputeIpv4GlobalRouting> routing = SatComputeIpv4GlobalRoutingHelper::GetRouting(
        m_idMap->GetNodeBySatelliteId(sourceSatelliteId));
    return routing->GetEffectiveRouteCandidates(GetServiceAddress(destinationSatelliteId));
}

uint32_t
ReplayTopologyController::GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                                uint32_t outputInterface) const
{
    RequireInitialized();
    const std::vector<IslDirectedLink>& links = m_linkState->GetDirectedLinks();
    auto link = std::find_if(links.begin(),
                             links.end(),
                             [sourceSatelliteId, outputInterface](const IslDirectedLink& item) {
                                 return item.sourceSatelliteId == sourceSatelliteId &&
                                        item.outputInterface == outputInterface;
                             });
    if (link == links.end())
    {
        throw ReplayTopologyControllerError(
            "output interface cannot be mapped to an ISL next hop");
    }
    return link->destinationSatelliteId;
}

uint64_t
ReplayTopologyController::GetIslDataRateBps(uint32_t sourceSatelliteId,
                                            uint32_t outputInterface) const
{
    RequireInitialized();
    Ptr<Node> source = m_idMap->GetNodeBySatelliteId(sourceSatelliteId);
    Ptr<Ipv4> ipv4 = source->GetObject<Ipv4>();
    if (ipv4 == nullptr || outputInterface >= ipv4->GetNInterfaces())
    {
        throw ReplayTopologyControllerError("ISL data-rate query has an invalid interface");
    }
    Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(ipv4->GetNetDevice(outputInterface));
    if (device == nullptr)
    {
        throw ReplayTopologyControllerError("ISL data-rate query target is not point-to-point");
    }
    DataRateValue dataRate;
    if (!device->GetAttributeFailSafe("DataRate", dataRate) ||
        dataRate.Get().GetBitRate() == 0)
    {
        throw ReplayTopologyControllerError("ISL data-rate query returned zero bandwidth");
    }
    return dataRate.Get().GetBitRate();
}

uint64_t
ReplayTopologyController::GetRouteEpoch(uint32_t satelliteId) const
{
    RequireInitialized();
    return SatComputeIpv4GlobalRoutingHelper::GetRouting(
               m_idMap->GetNodeBySatelliteId(satelliteId))
        ->GetRouteEpoch();
}

uint64_t
ReplayTopologyController::GetHashSeed() const
{
    return m_config.routing.hashSeed;
}

bool
ReplayTopologyController::IsCapacityAwareRouting() const
{
    return m_routingMode == RoutingMode::CAPACITY_AWARE_HRW;
}

uint32_t
ReplayTopologyController::GetAppliedSnapshotCount() const
{
    RequireInitialized();
    return m_appliedSnapshotCount;
}

uint32_t
ReplayTopologyController::GetAppliedTopologySliceCount() const
{
    return GetAppliedSnapshotCount();
}

uint32_t
ReplayTopologyController::GetRouteComputationCount() const
{
    RequireInitialized();
    return m_routeComputationCount;
}

const TopologyLinkUpdateSummary&
ReplayTopologyController::GetLastUpdateSummary() const
{
    RequireInitialized();
    return m_lastUpdateSummary;
}

} // namespace ns3
