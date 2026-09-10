/* SPDX-License-Identifier: GPL-2.0-only */
#include "first-feasible-placement-policy.h"

#include <algorithm>

namespace ns3::protection
{
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
