/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_DEFINITION_H
#define SATCOMPUTE_FAULT_DEFINITION_H

#include <cstdint>
#include <optional>

namespace ns3
{

/** Runtime resource scope changed by one occurred fault. */
enum class FaultType
{
    COMPUTE,
    SATELLITE
};

/** Convert one validated fault type to its JSON spelling. */
const char* FaultTypeToString(FaultType type);

/** One observed fault trace record; notifications are not events. */
struct FaultDefinition
{
    uint64_t faultId{}; ///< Positive trace identity.
    uint32_t nodeId{}; ///< Stable external satellite ID.
    FaultType faultType{FaultType::COMPUTE}; ///< Compute-only or whole-satellite scope.
    bool faultOccurred{true}; ///< Whether this record schedules START.
    std::optional<int64_t> startTimeNs; ///< Absolute occurred-fault time.
    std::optional<double> failureProbability; ///< Actual current-step union probability used at START.
    std::optional<int64_t> durationNs; ///< Recoverable outage duration.
    std::optional<double> pF1; ///< Actual F1 sampling probability at START.
    std::optional<double> pF2; ///< Actual F2 sampling probability at START.
    std::optional<double> temperatureC; ///< Protected thermal state at compute START.
    std::optional<double> continuousBusySeconds; ///< Uninterrupted busy duration before START.
    bool f1Occurred{}; ///< Observed independent F1 hit; runtime provenance, not a new event.
    bool f2Occurred{}; ///< Observed independent F2 hit; both may be true for one outage.

    /** @return Start plus duration, or null for non-recoverable/non-fault records. */
    std::optional<int64_t> GetRecoveryTimeNs() const;
    /** @return Required occurred-fault start time. */
    int64_t GetAnchorTimeNs() const;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_DEFINITION_H
