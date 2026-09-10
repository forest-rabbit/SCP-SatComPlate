/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PLACEMENT_POLICY_H
#define SATCOMPUTE_PLACEMENT_POLICY_H
#include "../common/protection-types.h"

namespace ns3::protection
{
/** Read-only current candidates. No future queue, fault trace or reservations. */
struct PlacementContext
{
    uint32_t primaryNode{};                  ///< Excluded original compute satellite.
    std::vector<BackupCandidate> candidates; ///< Stable-ID candidates with causal attributes.
};

/** Ordered local/remote pair; neither node is the primary, nor the same satellite. */
struct PlacementDecision
{
    uint32_t localNode{};  ///< One-hop local incremental state receiver.
    uint32_t remoteNode{}; ///< Reachable committed-state receiver.
    bool operator==(const PlacementDecision&) const = default;
};

/** Placement ranks candidates only; runtime owns real admission and execution. */
class PlacementPolicy
{
  public:
    virtual ~PlacementPolicy() = default;
    /** Return a complete pair, or no decision if either role cannot be placed. */
    virtual std::optional<PlacementDecision> Select(const PlacementContext& context) const = 0;
};

/** Shared baseline predicates, deliberately not storage/load optimization. */
inline bool IsPlacementCandidate(const BackupCandidate& node, uint32_t primary)
{
    return node.nodeId != primary && node.healthy && node.idle && node.reachable;
}
} // namespace ns3::protection
#endif
