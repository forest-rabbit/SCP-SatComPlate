/* SPDX-License-Identifier: GPL-2.0-only */
#include "least-recovery-load-placement-policy.h"

#include <algorithm>
#include <tuple>

namespace ns3::protection
{
LeastRecoveryLoadPlacementPolicy::LeastRecoveryLoadPlacementPolicy(uint32_t recoveryWeight)
    : m_recoveryWeight(recoveryWeight)
{
}

std::optional<PlacementDecision> LeastRecoveryLoadPlacementPolicy::Select(
    const PlacementContext& context) const
{
    auto nodes = context.candidates;
    const auto score = [&](const auto& node) {
        return static_cast<unsigned __int128>(node.backupAssignmentCount) +
               static_cast<unsigned __int128>(m_recoveryWeight) * node.activeRecoveryCount;
    };
    std::sort(nodes.begin(), nodes.end(), [&](const auto& a, const auto& b) {
        return std::tuple{score(a), a.nodeId} < std::tuple{score(b), b.nodeId};
    });
    const auto local = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) {
        return IsPlacementCandidate(node, context.primaryNode) && node.oneHop;
    });
    if (local == nodes.end())
        return std::nullopt;
    const auto remote = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) {
        return IsPlacementCandidate(node, context.primaryNode) && node.nodeId != local->nodeId;
    });
    if (remote == nodes.end())
        return std::nullopt;
    return PlacementDecision{local->nodeId, remote->nodeId};
}
} // namespace ns3::protection
