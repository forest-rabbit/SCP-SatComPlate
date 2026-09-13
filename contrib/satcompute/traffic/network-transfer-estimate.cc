/* SPDX-License-Identifier: GPL-2.0-only */
#include "network-transfer-engine.h"
#include "ns3/ipv4.h"
#include "ns3/point-to-point-channel.h"
#include <algorithm>
#include <limits>
#include <set>

namespace ns3
{
uint64_t NetworkTransferEngine::GetSentBytes(uint64_t id) const
{
    return m_senders.at(GetPlanIndex(id))->GetSentBytes();
}

std::optional<int64_t> NetworkTransferEngine::EstimateRemainingTransferTimeNs(uint64_t id) const
{
    const auto index = GetPlanIndex(id);
    if (IsCompleted(id)) return 0;
    if (IsTerminal(id) || (m_states[index] != TransferRuntimeState::ACTIVE &&
                           m_states[index] != TransferRuntimeState::SENDER_FINISHED)) return {};
    const auto& plan = m_plans[index];
    CapacityAwarePath path;
    if (m_capacityAwareRouting)
    {
        const auto existing = m_capacityReservationState->FindActivePath(id);
        if (!existing || !m_capacityReservationState->IsActivePathValid(
                id, plan.destinationSatelliteId, *m_topology)) return {};
        path = *existing;
    }
    else
    {
        if (!m_flowRouteRegistry) return {};
        path.admittedRateBps = std::numeric_limits<uint64_t>::max();
        std::set<uint32_t> visited;
        for (auto node = plan.sourceSatelliteId; node != plan.destinationSatelliteId;)
        {
            FlowRouteAssignment assignment;
            if (!visited.insert(node).second ||
                !m_flowRouteRegistry->FindAssignment(node, GetFlowKey(index), assignment)) return {};
            const auto routes = m_topology->GetEcmpRouteCandidates(node, plan.destinationSatelliteId);
            if (std::find(routes.begin(), routes.end(), assignment.candidate) == routes.end()) return {};
            const auto next = m_topology->GetNextHopSatelliteId(node, assignment.candidate.outputInterface);
            const auto rate = m_topology->GetIslDataRateBps(node, assignment.candidate.outputInterface);
            path.hops.push_back({node, next, assignment.candidate, rate});
            path.admittedRateBps = std::min(path.admittedRateBps, rate);
            node = next;
        }
    }
    if (!path.admittedRateBps || path.hops.empty() || !plan.payloadBytesPerPacket) return {};
    // Receiver-remaining includes already-sent but unreceived data. Charging its
    // serialization again is conservative under a fixed lossless path; it is NOT
    // a bound under arbitrary future queueing/loss. Never previews a fresh flow.
    const auto remaining = plan.sizeBytes - GetReceivedBytes(id);
    const auto packets = remaining / plan.payloadBytesPerPacket + bool(remaining % plan.payloadBytesPerPacket);
    const unsigned __int128 wire = remaining + static_cast<unsigned __int128>(30) * packets;
    unsigned __int128 delay = (wire * 8000000000ULL + path.admittedRateBps - 1) / path.admittedRateBps;
    for (const auto& hop : path.hops)
    {
        const auto ipv4 = m_topology->GetNodeBySatelliteId(hop.sourceSatelliteId)->GetObject<Ipv4>();
        const auto channel = DynamicCast<PointToPointChannel>(
            ipv4->GetNetDevice(hop.candidate.outputInterface)->GetChannel());
        if (!channel) return {};
        TimeValue propagation;
        channel->GetAttribute("Delay", propagation);
        if (propagation.Get().GetNanoSeconds() < 0) return {};
        delay += propagation.Get().GetNanoSeconds();
    }
    if (delay > std::numeric_limits<int64_t>::max()) return {};
    return static_cast<int64_t>(delay);
}
} // namespace ns3
