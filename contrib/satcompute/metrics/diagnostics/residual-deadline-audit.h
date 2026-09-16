/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RESIDUAL_DEADLINE_AUDIT_H
#define SATCOMPUTE_RESIDUAL_DEADLINE_AUDIT_H
#include "../../protection/policy/compfrr/frequency/compfrr-frequency-policy.h"
#include "../../protection/policy/compfrr/input/selective-input-snapshot.h"
#include "../../protection/policy/compfrr/input/input-admission-policy.h"

namespace ns3::protection
{
/** Model minimum over existing node/path/storage-feasible configurations, without a deadline filter. */
struct ResidualRecoveryMinimum
{
    std::optional<FrequencyConfiguration> config;
    std::optional<double> seconds;
    FrequencyStorageDemand storage;
    uint64_t resourceFeasible{}, storageRejected{};
};
ResidualRecoveryMinimum InspectMinimumRecovery(const FrequencyInput& input);

/** Passive candidate snapshot. Later fault/commit labels are joined only while writing. */
struct ResidualDeadlineAuditRecord
{
    size_t decisionIndex{};
    uint64_t candidateIndex{}, candidateCount{};
    FrequencyInput input; ///< The pure estimator is cleared immediately after inspection.
    ResidualRecoveryMinimum minimum;
    SelectiveInputSnapshot selective; ///< Hypothetical candidate, NOT a committed actual pair.
    InputAdmissionDecision selector;
    double slackSeconds{}, initializationSeconds{};
    std::string originalReason;
};
} // namespace ns3::protection
#endif
