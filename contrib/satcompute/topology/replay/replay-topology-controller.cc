/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "replay-topology-controller.h"

#include "../snapshot/snapshot-reader.h"
#include "../snapshot/snapshot-schedule.h"
#include "../../routing/ns3/satcompute-ipv4-global-routing-helper.h"

#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/simulator.h"

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
        m_routingMode != RoutingMode::HASH_PER_FLOW)
    {
        throw ReplayTopologyControllerError(
            "replay controller currently supports global-first and "
            "global-hash-per-flow only");
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
    const std::filesystem::path& linksFilename) const
{
    SatelliteSnapshot snapshot =
        ReadSatelliteSnapshot(nodesFilename, linksFilename);
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
        schedule.initialLinksFilename);

    const uint32_t satelliteCount = m_config.constellation.GetSatelliteCount();
    m_expectedSatelliteIds.resize(satelliteCount);
    std::iota(m_expectedSatelliteIds.begin(), m_expectedSatelliteIds.end(), 0);
    ValidateSatelliteIds(rawInitial, schedule.initialNodesFilename);

    m_nodes.Create(satelliteCount);
    m_idMap =
        std::make_unique<SatelliteIdMap>(m_nodes, m_expectedSatelliteIds);
    Ipv4StaticRoutingHelper staticRouting;
    SatComputeIpv4GlobalRoutingHelper globalRouting(
        m_routingMode,
        m_config.routing.hashSeed);
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

    SatelliteSnapshot initial = rawInitial;
    for (SatelliteLink& link : initial.links)
    {
        link.bandwidthBps = m_config.network.linkBandwidthBps;
        if (m_config.network.delayMode == "fixed")
        {
            link.delayNs = *m_config.network.fixedDelayNs;
        }
    }
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
                            update.nodesFilename,
                            update.linksFilename);
    }
}

void
ReplayTopologyController::ApplyScheduledSnapshot(
    std::filesystem::path nodesFilename,
    std::filesystem::path linksFilename)
{
    const SatelliteSnapshot snapshot =
        ReadAndNormalizeSnapshot(nodesFilename, linksFilename);
    m_lastUpdateSummary = m_linkState->ApplyFullSnapshot(snapshot.links);
    ++m_appliedSnapshotCount;
    if (m_lastUpdateSummary.ActiveEdgeSetChanged())
    {
        Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
        SatComputeIpv4GlobalRoutingHelper::AdvanceRouteEpoch(m_nodes);
        ++m_routeComputationCount;
    }
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

Ipv4Address
ReplayTopologyController::GetServiceAddress(uint32_t satelliteId) const
{
    RequireInitialized();
    return m_serviceMap->GetServiceAddress(satelliteId);
}

uint32_t
ReplayTopologyController::GetAppliedSnapshotCount() const
{
    RequireInitialized();
    return m_appliedSnapshotCount;
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
