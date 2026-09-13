/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_JIT_INPUT_STAGING_POLICY_H
#define SATCOMPUTE_JIT_INPUT_STAGING_POLICY_H
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ns3::protection
{
/** Causal conditional probability, not a realized future failure. */
struct FrequencyRiskStep
{
    int64_t targetTimeNs{};
    double combinedStepFailureProbability{};
};

/** Evaluation after known survival; the completed current check is always excluded. */
struct JitTimingDecision
{
    double probabilityOn{};
    std::optional<long double> representativeFaultTimeNs;
    bool shouldPrefetch{};
    std::string reason;
};

/** Pure event-aware rule. No timer, simulator, storage, network or RNG access. */
JitTimingDecision EvaluateJitTiming(int64_t nowNs, int64_t nextEvaluationNs,
                                   int64_t finishExclusiveNs, double inputSeconds,
                                   const std::vector<FrequencyRiskStep>& futureSteps);

/** One fixed-pair plan, shared by every frequency candidate; times are estimates only. */
struct JitInputPlan
{
    std::optional<int64_t> startNs;
    std::optional<long double> readyTimeNs;
    double readyProbability{}, deferredLossSeconds{}, prefetchLossSeconds{}, gainSeconds{};
};

/** Start at estimated state-ready, then only existing fault epochs; no new optimizer. */
JitInputPlan PlanJitInput(int64_t nowNs, int64_t stateReadyNs, int64_t finishExclusiveNs,
                         double inputSeconds, const std::vector<FrequencyRiskStep>& futureSteps,
                         bool currentlyAdmissible);
} // namespace ns3::protection
#endif
