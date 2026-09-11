/* SPDX-License-Identifier: GPL-2.0-only */
#include "placement-policy.h"

#include <map>
#include <stdexcept>

namespace ns3::protection
{
FeasiblePlacementPairs BuildFeasiblePlacementPairs(
    const PlacementContext& context,
    const PlacementPathPreview& preview)
{
    FeasiblePlacementPairs out;
    std::map<std::pair<uint32_t, uint32_t>, PlacementPathAvailability> cache;
    auto path = [&](uint32_t source, uint32_t destination) -> const PlacementPathAvailability& {
        const auto key = std::pair{source, destination};
        auto found = cache.find(key);
        if (found == cache.end())
            found = cache.emplace(key, preview ? preview(source, destination)
                                               : PlacementPathAvailability{true, true, ""}).first;
        return found->second;
    };
    for (const auto& local : context.candidates)
        for (const auto& remote : context.candidates)
        {
            if (local.nodeId == context.primaryNode || remote.nodeId == context.primaryNode ||
                local.nodeId == remote.nodeId) continue;
            ++out.total;
            if (!IsPlacementCandidate(local, context.primaryNode) || !local.oneHop ||
                !IsPlacementCandidate(remote, context.primaryNode))
            {
                ++out.skipNode;
                continue;
            }
            ++out.nodeFeasible;
            const auto a = path(context.primaryNode, local.nodeId);
            const auto b = path(context.primaryNode, remote.nodeId);
            const auto c = path(local.nodeId, remote.nodeId);
            if (!a.reachable || !b.reachable || !c.reachable) ++out.skipNoRoute;
            else if (a.admissible && b.admissible && c.admissible)
                out.pairs.push_back({local.nodeId, remote.nodeId});
            else
            {
                bool capacity = true;
                for (const auto& p : {a, b, c})
                    if (!p.admissible && p.reason != "NO_ADMISSIBLE_PATH") capacity = false;
                if (capacity) ++out.skipNoCapacity;
                else ++out.skipOther;
            }
        }
    if (!out.nodeFeasible) out.reason = "NO_FEASIBLE_NODE_PAIR";
    else if (!out.pairs.empty()) out.reason.clear();
    else if (out.skipNoCapacity) out.reason = "NO_CAPACITY_NOW";
    else if (out.skipOther) out.reason = "PATH_ADMISSION_UNAVAILABLE";
    else out.reason = "NO_ROUTE";
    return out;
}

std::vector<uint32_t> BuildFeasibleBackupNodes(
    const PlacementContext& context, const std::function<bool(uint32_t)>& operationFeasible)
{
    std::vector<uint32_t> nodes;
    for (const auto& node : context.candidates)
        if (IsPlacementCandidate(node, context.primaryNode) &&
            (!operationFeasible || operationFeasible(node.nodeId)))
            nodes.push_back(node.nodeId);
    return nodes;
}

std::optional<PlacementDecision> PlacementPolicy::SelectCheckpointPair(
    const PlacementContext& context, const PlacementPathPreview& preview) const
{
    auto pairs = BuildFeasiblePlacementPairs(context, preview).pairs;
    RankPairs(pairs, context);
    return pairs.empty() ? std::nullopt : std::optional{pairs.front()};
}

std::optional<uint32_t> PlacementPolicy::SelectBackupNode(
    const PlacementContext& context, const std::function<bool(uint32_t)>& operationFeasible) const
{
    auto nodes = BuildFeasibleBackupNodes(context, operationFeasible);
    RankBackupNodes(nodes, context);
    return nodes.empty() ? std::nullopt : std::optional{nodes.front()};
}
} // namespace ns3::protection
