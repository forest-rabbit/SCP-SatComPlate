/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PLACEMENT_POLICY_H
#define SATCOMPUTE_PLACEMENT_POLICY_H
#include "../common/protection-types.h"
#include <functional>
#include <filesystem>

namespace ns3::protection
{
/** Candidate filtering is orthogonal to ranking and never inferred from a name. */
enum class PlacementEligibility { MINIMAL, FEASIBILITY_AWARE };
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

/** Exhaustive node/path counts; frequency hard constraints are checked in ranking order. */
struct FeasiblePlacementPairs
{
    std::vector<PlacementDecision> pairs;
    uint64_t total{}, nodeFeasible{}, skipNode{}, skipNoRoute{}, skipNoCapacity{}, skipOther{};
    std::string reason;
};

/** Observational decision ledger. Admission here is synchronous resource admission,
 * not successful asynchronous INPUT/checkpoint delivery. Never read by ranking. */
struct PlacementSelection
{
    uint64_t taskId{};
    int64_t timeNs{};
    uint32_t primary{};
    std::optional<PlacementDecision> pair;
    std::optional<uint32_t> node;
    std::string admission, reason;
};

FeasiblePlacementPairs BuildFeasiblePlacementPairs(
    const PlacementContext& context,
    const PlacementPathPreview& preview = {});

/** Healthy/idle and structural constraints only. Never invokes a path preview. */
FeasiblePlacementPairs BuildMinimalPlacementPairs(const PlacementContext& context);
std::vector<uint32_t> BuildMinimalBackupNodes(const PlacementContext& context);

/** Common node filters plus operation-specific route/storage/deadline requirements. */
std::vector<uint32_t> BuildFeasibleBackupNodes(
    const PlacementContext& context,
    const std::function<bool(uint32_t)>& operationFeasible = {});

/** Placement ranks candidates only; runtime owns real admission and execution. */
class PlacementPolicy
{
  public:
    virtual ~PlacementPolicy() = default;
    virtual const char* Name() const = 0; ///< Stable diagnostic name, not an eligibility rule.
    virtual PlacementEligibility Eligibility() const { return PlacementEligibility::MINIMAL; }
    FeasiblePlacementPairs BuildPairs(const PlacementContext& context,
                                     const PlacementPathPreview& preview = {}) const;
    void RecordSelection(PlacementSelection row) { m_selections.push_back(std::move(row)); }
    void RecordAdmission(uint64_t task, int64_t time, const std::string& status,
                         const std::string& reason);
    void WriteSelections(const std::filesystem::path& directory) const;
    const std::vector<PlacementSelection>& Selections() const { return m_selections; }
    /** Return a complete pair, or no decision if either role cannot be placed. */
    std::optional<PlacementDecision> SelectCheckpointPair(
        const PlacementContext& context, const PlacementPathPreview& preview = {}) const;
    /** Single backup/recompute/replica role; does not fabricate a checkpoint pair. */
    std::optional<uint32_t> SelectBackupNode(
        const PlacementContext& context,
        const std::function<bool(uint32_t)>& operationFeasible = {}) const;
    /** Rank a common prefiltered single-node set. */
    virtual void RankBackupNodes(std::vector<uint32_t>& nodes,
                                 const PlacementContext& context) const = 0;
    /** Rank only the common prefiltered set; no objective/risk optimization here. */
    virtual void RankPairs(std::vector<PlacementDecision>& pairs,
                           const PlacementContext& context) const = 0;
  private:
    std::vector<PlacementSelection> m_selections; ///< Diagnostics, not a policy input.
};

/** Shared baseline predicates, deliberately not storage/load optimization. */
inline bool IsPlacementCandidate(const BackupCandidate& node, uint32_t primary)
{
    return node.nodeId != primary && node.healthy && node.idle && node.reachable;
}
} // namespace ns3::protection
#endif
