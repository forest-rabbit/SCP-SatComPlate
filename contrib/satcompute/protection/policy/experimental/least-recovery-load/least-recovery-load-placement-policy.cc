/* SPDX-License-Identifier: GPL-2.0-only */
#include "least-recovery-load-placement-policy.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace ns3::protection
{
void LeastRecoveryLoadPlacementPolicy::RankPairs(std::vector<PlacementDecision>& pairs,
                                                const PlacementContext& context) const
{
    std::map<uint32_t, unsigned __int128> loads;
    for (const auto& node : context.candidates)
        loads[node.nodeId] = static_cast<unsigned __int128>(node.backupAssignmentCount) +
                            static_cast<unsigned __int128>(m_recoveryWeight) * node.activeRecoveryCount;
    // Preserve the existing local-first, then remote load ranking and stable-ID ties.
    auto key = [&](const auto& p) {
        return std::tuple{loads.at(p.localNode), p.localNode, loads.at(p.remoteNode), p.remoteNode};
    };
    std::sort(pairs.begin(), pairs.end(), [&](const auto& a, const auto& b) { return key(a) < key(b); });
}

LeastRecoveryLoadPlacementPolicy::LeastRecoveryLoadPlacementPolicy(uint32_t recoveryWeight)
    : m_recoveryWeight(recoveryWeight)
{
}

void LeastRecoveryLoadPlacementPolicy::RankBackupNodes(
    std::vector<uint32_t>& nodes, const PlacementContext& context) const
{
    std::map<uint32_t, unsigned __int128> loads;
    for (const auto& node : context.candidates)
        loads[node.nodeId] = static_cast<unsigned __int128>(node.backupAssignmentCount) +
                            static_cast<unsigned __int128>(m_recoveryWeight) * node.activeRecoveryCount;
    std::sort(nodes.begin(), nodes.end(), [&](const auto& a, const auto& b) {
        return std::tuple{loads.at(a), a} < std::tuple{loads.at(b), b};
    });
}
} // namespace ns3::protection
