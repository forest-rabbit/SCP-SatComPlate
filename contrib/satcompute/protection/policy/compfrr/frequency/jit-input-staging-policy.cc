/* SPDX-License-Identifier: GPL-2.0-only */
#include "jit-input-staging-policy.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
void Validate(int64_t now, int64_t finish, double seconds,
              const std::vector<FrequencyRiskStep>& steps)
{
    if (now < 0 || finish < now || !std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("invalid JIT time domain");
    int64_t previous = -1;
    for (const auto& step : steps)
    {
        const auto q = step.combinedStepFailureProbability;
        if (step.targetTimeNs < 0 || step.targetTimeNs <= previous ||
            !std::isfinite(q) || q < 0 || q > 1)
            throw std::invalid_argument("invalid JIT probability trajectory");
        previous = step.targetTimeNs;
    }
}
} // namespace

JitTimingDecision EvaluateJitTiming(int64_t now, int64_t next, int64_t finish,
                                   double seconds, const std::vector<FrequencyRiskStep>& steps)
{
    Validate(now, finish, seconds, steps);
    if (next <= now) throw std::invalid_argument("JIT next event must be in the future");
    long double survival = 1, mass = 0, weighted = 0;
    for (const auto& step : steps)
    {
        // Restart survival at one after the actual completed sample. Also exclude
        // the compute finish boundary, where the primary no longer needs protection.
        if (step.targetTimeNs <= now || step.targetTimeNs >= finish) continue;
        const auto w = survival * step.combinedStepFailureProbability;
        survival *= 1 - static_cast<long double>(step.combinedStepFailureProbability);
        mass += w;
        weighted += w * step.targetTimeNs;
    }
    JitTimingDecision out;
    out.probabilityOn = static_cast<double>(mass);
    out.reason = "NO_FUTURE_FAULT_MASS";
    if (mass > 0)
    {
        out.representativeFaultTimeNs = weighted / mass;
        out.shouldPrefetch = *out.representativeFaultTimeNs <= next + seconds * 1e9L;
        out.reason = out.shouldPrefetch ? "EVENT_AWARE_JIT" : "WAIT_NEXT_KNOWN_EVENT";
    }
    return out;
}

JitInputPlan PlanJitInput(int64_t now, int64_t ready, int64_t finish, double seconds,
                         const std::vector<FrequencyRiskStep>& steps, bool admissible)
{
    Validate(now, finish, seconds, steps);
    if (ready < now) throw std::invalid_argument("JIT state-ready precedes START");
    JitInputPlan out;
    if (admissible && ready < finish)
    {
        std::vector<int64_t> evaluations{ready};
        for (const auto& step : steps)
            if (step.targetTimeNs > ready && step.targetTimeNs < finish)
                evaluations.push_back(step.targetTimeNs);
        for (size_t i = 0; i < evaluations.size(); ++i)
        {
            const auto next = i + 1 < evaluations.size() ? evaluations[i + 1] : finish;
            if (EvaluateJitTiming(evaluations[i], next, finish, seconds, steps).shouldPrefetch)
            {
                out.startNs = evaluations[i];
                out.readyTimeNs = evaluations[i] + seconds * 1e9L;
                break;
            }
        }
    }
    long double survival = 1, readyMass = 0, loss = 0;
    for (const auto& step : steps)
    {
        // Preserve the supplied START predictor's original endpoint convention.
        // The runtime ON window is separately finish-exclusive above.
        if (step.targetTimeNs < now || step.targetTimeNs > finish) continue;
        const auto w = survival * step.combinedStepFailureProbability;
        survival *= 1 - static_cast<long double>(step.combinedStepFailureProbability);
        if (step.targetTimeNs < ready) continue;
        readyMass += w;
        const long double remaining = out.readyTimeNs
            ? std::clamp((*out.readyTimeNs - step.targetTimeNs) / 1e9L, 0.L,
                         static_cast<long double>(seconds)) : seconds;
        loss += w * remaining;
    }
    out.readyProbability = static_cast<double>(readyMass);
    out.deferredLossSeconds = static_cast<double>(readyMass * seconds);
    out.prefetchLossSeconds = static_cast<double>(loss);
    // Exact degeneration, not subtraction noise masquerading as a START benefit.
    out.gainSeconds = out.startNs ? std::max(0.0, out.deferredLossSeconds - out.prefetchLossSeconds) : 0;
    return out;
}
} // namespace ns3::protection
