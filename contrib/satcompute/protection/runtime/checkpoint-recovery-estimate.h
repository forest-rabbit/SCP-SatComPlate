/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CHECKPOINT_RECOVERY_ESTIMATE_H
#define SATCOMPUTE_CHECKPOINT_RECOVERY_ESTIMATE_H
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace ns3::protection
{
/** Pure time arithmetic: callers supply current paths, legal state and compute durations. */
struct CheckpointRecoveryEstimate
{
    std::optional<int64_t> redoNs, tailNs; ///< Catch-up estimates; absent means unavailable.
    int64_t postNs{}, budgetNs{}; ///< Post-catch-up compute and remaining catch-up budget.
    bool redoFits{}, tailFits{}; ///< Inclusive original compute deadline.
};

/** No admission, state mutation, candidate selection or random sampling. */
inline CheckpointRecoveryEstimate
EvaluateCheckpointRecovery(std::optional<int64_t> inputNs,
                           std::optional<int64_t> stateNs,
                           std::optional<int64_t> tailNs,
                           int64_t fusionNs,
                           int64_t redoComputeNs,
                           int64_t tailComputeNs,
                           int64_t postNs,
                           int64_t remainingNs)
{
    if (fusionNs < 0 || redoComputeNs < 0 || tailComputeNs < 0 || postNs < 0 || remainingNs < 0 ||
        (inputNs && *inputNs < 0) || (stateNs && *stateNs < 0) || (tailNs && *tailNs < 0))
        throw std::invalid_argument("negative checkpoint recovery duration");
    const auto add = [](int64_t a, int64_t b) -> std::optional<int64_t> {
        if (b > std::numeric_limits<int64_t>::max() - a) return std::nullopt;
        return a + b;
    };
    CheckpointRecoveryEstimate result;
    result.postNs = postNs;
    result.budgetNs = remainingNs - postNs;
    if (inputNs && stateNs)
    {
        result.redoNs = add(std::max(*inputNs, *stateNs), redoComputeNs);
        if (tailNs)
            if (const auto join = add(std::max(*stateNs, *tailNs), fusionNs))
                result.tailNs = add(std::max(*inputNs, *join), tailComputeNs);
    }
    result.redoFits = result.redoNs && *result.redoNs <= result.budgetNs;
    result.tailFits = result.tailNs && *result.tailNs <= result.budgetNs;
    return result;
}
} // namespace ns3::protection
#endif
