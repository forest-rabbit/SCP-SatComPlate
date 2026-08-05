/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "satellite-topology.h"

#include "online/online-topology-controller.h"
#include "satellite-id-map.h"

#include <algorithm>
#include <utility>

namespace ns3
{

SatelliteTopology::SatelliteTopology(const SatComputeConfig& config,
                                     const ConstellationDefinition& constellation)
    : m_controller(std::make_unique<OnlineTopologyController>(config, constellation))
{
}

SatelliteTopology::~SatelliteTopology() = default;

void
SatelliteTopology::Initialize()
{
    m_controller->Initialize();
}

void
SatelliteTopology::RegisterRouteUpdateCallback(Callback<void> callback)
{
    m_controller->RegisterRouteUpdateCallback(callback);
}

void
SatelliteTopology::InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const
{
    m_controller->InvalidateFlowRouteDecisionCache(flowKey);
}

const NodeContainer&
SatelliteTopology::GetNodes() const
{
    return m_controller->GetNodes();
}

const SatelliteIdMap&
SatelliteTopology::GetIdMap() const
{
    return m_controller->GetIdMap();
}

const SatelliteLinkState&
SatelliteTopology::GetLinkState() const
{
    return m_controller->GetLinkState();
}

const OnlineOrbitConstellation&
SatelliteTopology::GetOnlineConstellation() const
{
    return m_controller->GetConstellation();
}

uint32_t
SatelliteTopology::GetNodeCount() const
{
    return GetIdMap().GetNodeCount();
}

Ptr<Node>
SatelliteTopology::GetNode(uint32_t index) const
{
    return GetIdMap().GetNodeByIndex(index);
}

Ipv4Address
SatelliteTopology::GetServiceAddress(uint32_t index) const
{
    return GetServiceAddressBySatelliteId(GetSatelliteIdByNodeIndex(index));
}

bool
SatelliteTopology::HasSatelliteId(uint32_t satelliteId) const
{
    return m_controller->HasSatelliteId(satelliteId);
}

uint32_t
SatelliteTopology::GetNodeIndexBySatelliteId(uint32_t satelliteId) const
{
    return GetIdMap().GetNodeIndexBySatelliteId(satelliteId);
}

uint32_t
SatelliteTopology::GetSatelliteIdByNodeIndex(uint32_t index) const
{
    return GetIdMap().GetSatelliteIdByNodeIndex(index);
}

Ptr<Node>
SatelliteTopology::GetNodeBySatelliteId(uint32_t satelliteId) const
{
    return m_controller->GetNodeBySatelliteId(satelliteId);
}

Ipv4Address
SatelliteTopology::GetServiceAddressBySatelliteId(uint32_t satelliteId) const
{
    return m_controller->GetServiceAddressBySatelliteId(satelliteId);
}

std::vector<uint32_t>
SatelliteTopology::GetEcmpCandidateSatelliteIds(uint32_t sourceSatelliteId,
                                                uint32_t destinationSatelliteId) const
{
    const std::vector<EcmpRouteCandidate> routes =
        GetEcmpRouteCandidates(sourceSatelliteId, destinationSatelliteId);
    std::vector<uint32_t> candidates;
    candidates.reserve(routes.size());
    for (const EcmpRouteCandidate& route : routes)
    {
        candidates.push_back(GetNextHopSatelliteId(sourceSatelliteId, route.outputInterface));
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::vector<EcmpRouteCandidate>
SatelliteTopology::GetEcmpRouteCandidates(uint32_t sourceSatelliteId,
                                          uint32_t destinationSatelliteId) const
{
    return m_controller->GetEcmpRouteCandidates(sourceSatelliteId, destinationSatelliteId);
}

uint32_t
SatelliteTopology::GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                         uint32_t outputInterface) const
{
    return m_controller->GetNextHopSatelliteId(sourceSatelliteId, outputInterface);
}

uint64_t
SatelliteTopology::GetIslDataRateBps(uint32_t sourceSatelliteId,
                                     uint32_t outputInterface) const
{
    return m_controller->GetIslDataRateBps(sourceSatelliteId, outputInterface);
}

uint64_t
SatelliteTopology::GetRouteEpoch(uint32_t satelliteId) const
{
    return m_controller->GetRouteEpoch(satelliteId);
}

uint64_t
SatelliteTopology::GetEcmpHashSeed() const
{
    return GetHashSeed();
}

uint64_t
SatelliteTopology::GetHashSeed() const
{
    return m_controller->GetHashSeed();
}

bool
SatelliteTopology::IsCapacityAwareRouting() const
{
    return m_controller->IsCapacityAwareRouting();
}

const std::vector<IslDirectedLink>&
SatelliteTopology::GetIslDirectedLinks() const
{
    return GetLinkState().GetDirectedLinks();
}

const std::vector<IslQueueDropEvent>&
SatelliteTopology::GetIslQueueDropEvents() const
{
    return GetLinkState().GetQueueDropEvents();
}

Ptr<FlowRouteRegistry>
SatelliteTopology::GetFlowRouteRegistry() const
{
    return m_controller->GetFlowRouteRegistry();
}

uint32_t
SatelliteTopology::GetAppliedTopologySliceCount() const
{
    return m_controller->GetAppliedTopologySliceCount();
}

uint32_t
SatelliteTopology::GetRouteComputationCount() const
{
    return m_controller->GetRouteComputationCount();
}

} // namespace ns3
