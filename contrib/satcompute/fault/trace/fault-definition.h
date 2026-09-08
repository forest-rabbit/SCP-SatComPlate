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

/** One v1/v2 risk or deterministic-fault trace record. */
struct FaultDefinition
{
    uint64_t faultId{}; ///< Positive trace identity.
    uint32_t nodeId{}; ///< Stable external satellite ID.
    FaultType faultType{FaultType::COMPUTE}; ///< Compute-only or whole-satellite scope.
    bool faultOccurred{true}; ///< Whether this record schedules START.
    std::optional<int64_t> noticeTimeNs; ///< Absolute risk-entry time.
    std::optional<int64_t> startTimeNs; ///< Absolute occurred-fault time.
    std::optional<double> failureProbability; ///< Probability captured at notice/start.
    std::optional<int64_t> warningLeadTimeNs; ///< Start minus notice.
    std::optional<int64_t> riskDurationNs; ///< Risk-clear minus notice.
    std::optional<int64_t> durationNs; ///< Recoverable outage duration.
    bool f1Occurred{}; ///< Observed independent F1 hit; runtime provenance, not a new event.
    bool f2Occurred{}; ///< Observed independent F2 hit; both may be true for one outage.

    /** @return Start plus duration, or null for non-recoverable/non-fault records. */
    std::optional<int64_t> GetRecoveryTimeNs() const;
    /** @return Start minus notice when both are present. */
    std::optional<int64_t> GetWarningLeadTimeNs() const;
    /** @return Notice plus risk duration for a risk-only record. */
    std::optional<int64_t> GetRiskClearTimeNs() const;
    /** @return Notice when present, otherwise the required start time. */
    int64_t GetAnchorTimeNs() const;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_DEFINITION_H
