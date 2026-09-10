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

std::optional<PlacementDecision> FirstFeasiblePlacementPolicy::Select(
    const PlacementContext& context) const
{
    auto nodes = context.candidates;
    std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
        return a.nodeId < b.nodeId;
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
