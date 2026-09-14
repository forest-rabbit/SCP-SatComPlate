/* SPDX-License-Identifier: GPL-2.0-only */
#include "first-feasible-placement-policy.h"

#include <algorithm>
#include <tuple>

namespace ns3::protection
{
void FirstFeasiblePlacementPolicy::RankPairs(std::vector<PlacementDecision>& pairs,
                                           const PlacementContext&) const
{
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return std::tie(a.localNode, a.remoteNode) < std::tie(b.localNode, b.remoteNode);
    });
}

void FirstFeasiblePlacementPolicy::RankBackupNodes(std::vector<uint32_t>& nodes,
                                                   const PlacementContext&) const
{
    std::sort(nodes.begin(), nodes.end());
}
} // namespace ns3::protection
