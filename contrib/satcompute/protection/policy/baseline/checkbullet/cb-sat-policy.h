/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CB_SAT_POLICY_H
#define SATCOMPUTE_CB_SAT_POLICY_H
#include "../../../common/task-state-adapter.h"
#include <map>
#include <string>

namespace ns3::protection::checkbullet
{
/** Both nominal cadence and the next admissible application boundary are retained. */
struct CbTarget
{
    uint32_t nominalPermille{}; ///< Fraction of original total WU, not a relative shift.
    uint64_t work{};            ///< Actual legal captured target, strictly below total WU.
};

/** Pure MTBF/cost decision; an empty target list means no periodic checkpoint. */
struct CbInterval
{
    double mtbfSeconds{}, referenceCostSeconds{}, taskSeconds{}, intervalSeconds{}, rawFraction{};
    uint32_t deltaPermille{};
    std::vector<CbTarget> targets;
    std::string reason;
};

/** Compute H once, preserving the common integer service-time and application rules. */
CbInterval SolveInterval(const TaskStateAdapter& layout, uint64_t primaryRate,
                         double mtbfSeconds);

/** State reconstruction only. INPUT/network/queue/redo are not included. */
struct CbRestoreCosts
{
    int64_t baseNs{};       ///< Explicit base/context read, zero in the starting profile.
    int64_t readPerLogNs{}; ///< Non-merge per-log read, zero in the starting profile.
    int64_t mergeNs{};      ///< Common cR; charged once for a nonempty constant batch.
    bool linearMerge{};    ///< Test/explicit alternative: mergeNs per log, not a hidden default.
    /** Checked duration for exactly count logs; an empty set has no merge. */
    int64_t Duration(uint64_t count) const;
};

/** X_R may be unbounded; storageLimit is evaluated over real remaining log shapes. */
struct CbThreshold
{
    std::optional<uint64_t> recoveryLimit;
    uint64_t storageLimit{}, naturalLimit{}, implementationLimit{}, value{};
    bool feasible{}, recoveryBinds{}, storageBinds{}, capBinds{};
    std::string reason;
};

/** Find a feasible positive X, never clamp a failed budget to one.
 * heldBytes includes the complete INPUT, current root and any required extra reservations.
 * logBytes is the actual next legal chain, not fictitious terminal checkpoints.
 */
CbThreshold SolveThreshold(uint64_t quotaBytes, uint64_t heldBytes,
                           const std::vector<uint64_t>& logBytes, int64_t restoreBudgetNs,
                           const CbRestoreCosts& costs,
                           std::optional<uint64_t> implementationCap = std::nullopt);

/** Occupancy-preserving equal shares of free capacity; stable IDs receive remainders. */
std::map<uint64_t, uint64_t> ShareStorage(uint64_t capacity,
                                       const std::map<uint64_t, uint64_t>& ownerOccupancy);
} // namespace ns3::protection::checkbullet
#endif
