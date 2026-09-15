/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_INPUT_ADMISSION_POLICY_H
#define SATCOMPUTE_INPUT_ADMISSION_POLICY_H
#include "../../../../traffic/network-transfer-engine.h"
#include "../../../../fault/model/compute-failure-predictor.h"

namespace ns3::protection
{
/** Optional INPUT screening, independent of checkpoint EAGER/DEFERRED layout. */
enum class InputAdmissionPolicy { NONE, SER_SYMMETRIC_BREAK_EVEN };
InputAdmissionPolicy ParseInputAdmissionPolicy(const std::string& name);
const char* ToString(InputAdmissionPolicy policy);

/** Immutable actual-pair pre-initialization causal inputs; no observed outcomes. */
struct InputAdmissionInput
{
    int64_t startNs{}, remainingNs{}, firstSampleNs{};
    bool finishExclusive{};
    uint64_t bytes{};
    AdmissiblePathEstimate path;
    std::optional<ComputeFailurePrediction> prediction;
};

/** Fractional expected nanoseconds are never rounded before the strict comparison. */
struct InputAdmissionDecision
{
    bool send{};
    std::string reason;
    int64_t serializationNs{}, networkReadyNs{};
    long double serialGainNs{}, networkGainNs{}, costNs{};
};

/** Pure binary rule. No timer, RNG, allocation, path query, or frequency solve. */
InputAdmissionDecision EvaluateInputAdmission(InputAdmissionPolicy policy,
                                               const InputAdmissionInput& input);
} // namespace ns3::protection
#endif
