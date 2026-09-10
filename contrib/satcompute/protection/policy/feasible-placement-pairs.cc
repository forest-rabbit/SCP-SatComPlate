/* SPDX-License-Identifier: GPL-2.0-only */
#include "placement-policy.h"

#include <map>
#include <stdexcept>

namespace ns3::protection
{
FeasiblePlacementPairs BuildFeasiblePlacementPairs(
    const PlacementContext& context,
    const std::function<PlacementPathAvailability(uint32_t, uint32_t)>& preview)
{
    FeasiblePlacementPairs out;
    std::map<std::pair<uint32_t, uint32_t>, PlacementPathAvailability> cache;
    auto path = [&](uint32_t source, uint32_t destination) -> const PlacementPathAvailability& {
        const auto key = std::pair{source, destination};
        auto found = cache.find(key);
        if (found == cache.end()) found = cache.emplace(key, preview(source, destination)).first;
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
} // namespace ns3::protection
