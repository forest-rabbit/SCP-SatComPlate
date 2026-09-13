/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-frequency-policy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace ns3::protection
{
namespace
{
/** Reject invalid probability instead of clamping a mismatched causal input. */
void Probability(double value)
{
    if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::invalid_argument("frequency probability outside [0,1]");
}

void Validate(const FrequencyInput& in)
{
    Probability(in.risk.qCurrentSample);
    Probability(in.risk.pFailBeforeFinish);
    for (double value : {in.inputBytes,
                         in.work,
                         in.variableBytes,
                         in.progress,
                         in.primaryRate,
                         in.recoveryRate,
                         in.inputBandwidth,
                         in.backupBandwidth,
                         in.remainingSeconds,
                         in.baseTransferSeconds,
                         in.stateTransferSeconds})
        if (!std::isfinite(value) || value < 0)
            throw std::invalid_argument("invalid finite nonnegative frequency input");
    if (in.work <= 0 || in.primaryRate <= 0 || in.recoveryRate <= 0 ||
        (in.replayAvailable && in.inputBandwidth <= 0) || in.backupBandwidth <= 0 ||
        in.progress > 1 || in.risk.epochNs < 0 || in.risk.intervalNs <= 0 || in.deadlineNs < 0 ||
        in.costs.localNs < 0 || in.costs.remoteNs < 0 || !in.storageDemand)
        throw std::invalid_argument("invalid frequency domain or missing storage estimator");
}

/** Aggregate existing predictions once per pair, preserving pre-ready survival mass. */
void StartWindow(const FrequencyInput& in, FrequencyDecision& out)
{
    const long double delayNs = std::ceil(static_cast<long double>(out.initializationSeconds) * 1e9L);
    if (delayNs < 0 || delayNs > std::numeric_limits<int64_t>::max() - in.risk.epochNs)
        throw std::invalid_argument("initialization ready time overflow");
    out.initReadyTimeNs = in.risk.epochNs + static_cast<int64_t>(delayNs);
    long double survival = 1, total = 0, ready = 0, weighted = 0;
    int64_t previous = -1;
    for (const auto& step : in.risk.futureSteps)
    {
        Probability(step.combinedStepFailureProbability);
        if (step.targetTimeNs < in.risk.epochNs || step.targetTimeNs <= previous)
            throw std::invalid_argument("START trajectory times must be causal and strictly ordered");
        previous = step.targetTimeNs;
        const auto mass = survival * step.combinedStepFailureProbability;
        survival *= 1 - static_cast<long double>(step.combinedStepFailureProbability);
        total += mass;
        if (step.targetTimeNs >= *out.initReadyTimeNs)
        {
            const long double progress = std::min(1.L, static_cast<long double>(in.progress) +
                static_cast<long double>(in.primaryRate) * (step.targetTimeNs - in.risk.epochNs) /
                    (1e9L * in.work));
            ready += mass;
            weighted += mass * progress;
        }
    }
    if (std::abs(total - in.risk.pFailBeforeFinish) > 1e-12L)
        throw std::invalid_argument("START trajectory disagrees with canonical P_finish");
    out.pFailAfterInitReady = static_cast<double>(ready);
    if (ready > 0)
        out.representativeProgressAfterReady = static_cast<double>(weighted / ready);
    if (in.replayAvailable)
        out.jOff = static_cast<double>(weighted) * in.work / in.recoveryRate +
            (in.inputPolicy == InputStagingPolicy::EAGER
                 ? static_cast<double>(ready) * in.inputBytes / in.inputBandwidth : 0);
    else if (ready == 0)
        out.jOff = 0;
    else
        out.jOff.reset(); // Preserve eager executable protection when OFF INPUT is unavailable.
}
} // namespace

FrequencyRisk MakeFrequencyRisk(double currentSamplerQ, const ComputeFailurePredictionInput& in)
{
    Probability(currentSamplerQ);
    const auto prediction = PredictComputeFailureBeforeFinish(in);
    if (currentSamplerQ != prediction.combinedStepFailureProbability)
        throw std::invalid_argument("policy q differs from current sampler q");
    FrequencyRisk risk{in.predictionTimeNs, in.checkIntervalNs,
                       currentSamplerQ, prediction.predictedFailureProbability, {}};
    risk.futureSteps.reserve(prediction.steps.size());
    for (const auto& step : prediction.steps)
        risk.futureSteps.push_back({step.targetTimeNs, step.combinedStepFailureProbability});
    return risk;
}

bool FrequencyCandidateLess(const FrequencyCandidate& a, const FrequencyCandidate& b)
{
    return std::tie(a.objective, a.config.deltaPermille, a.config.batchN) <
           std::tie(b.objective, b.config.deltaPermille, b.config.batchN);
}

FrequencyDecision CompFrrFrequencyPolicy::Evaluate(const FrequencyInput& in) const
{
    FrequencyDecision out;
    out.phase = in.phase;
    out.epochNs = in.risk.epochNs;
    if (in.phase != ProtectionPhase::OFF && in.phase != ProtectionPhase::ON)
    {
        out.reason = "NOT_DECISION_PHASE";
        return out;
    }
    Validate(in);
    const bool on = in.phase == ProtectionPhase::ON;
    const bool deferred = StateOnlyInitialization(in.inputPolicy);
    const bool jit = in.inputPolicy == InputStagingPolicy::JIT;
    const double fullInput = jit && in.inputTransferSeconds ? *in.inputTransferSeconds
        : in.replayAvailable ? in.inputBytes / in.inputBandwidth : 0;
    const double faultInput = jit && on && in.actualInputWaitSeconds
        ? *in.actualInputWaitSeconds
        : deferred && in.replayAvailable ? fullInput : 0;
    if (!std::isfinite(faultInput) || faultInput < 0)
        throw std::invalid_argument("invalid actual INPUT wait");
    const double cL = in.costs.localNs / 1e9;
    const double cR = in.costs.remoteNs / 1e9;
    const double interval = in.risk.intervalNs / 1e9;
    out.deadlineSlackSeconds =
        (in.deadlineNs - in.risk.epochNs) / 1e9 - in.work * (1 - in.progress) / in.recoveryRate;
    out.initializationSeconds = (deferred ? cL + in.stateTransferSeconds
                                         : std::max(in.baseTransferSeconds, cL + in.stateTransferSeconds)) + cR;
    if (in.replayAvailable)
        out.jOff = in.risk.pFailBeforeFinish *
                   (in.inputBytes / in.inputBandwidth + in.progress * in.work / in.recoveryRate);
    else if (in.risk.pFailBeforeFinish == 0)
        out.jOff = 0;
    if (!std::isfinite(out.deadlineSlackSeconds) || !std::isfinite(out.initializationSeconds) ||
        (out.jOff && !std::isfinite(*out.jOff)))
        throw std::invalid_argument("frequency time or OFF estimate overflow");
    out.legacyCurrentProgressLoss = out.jOff;
    if (in.progress >= 1 || in.remainingSeconds <= 0)
    {
        out.reason = "NO_REMAINING_COMPUTE";
        return out;
    }
    if (!on)
    {
        StartWindow(in, out);
        if (jit && in.replayAvailable)
        {
            const auto finish = in.risk.epochNs + static_cast<int64_t>(std::ceil(in.remainingSeconds * 1e9L));
            out.inputPlan = PlanJitInput(in.risk.epochNs, *out.initReadyTimeNs, finish,
                fullInput, in.risk.futureSteps,
                in.jitStartBenefit && in.jitPrefetchAdmissible);
            // Equivalent Delta-J form: omit common deferred INPUT cost from both
            // sides, then credit only the same fixed plan's risk-weighted saving.
            if (out.jOff) *out.jOff += out.inputPlan->gainSeconds;
        }
    }
    if (in.nodeAvailable && in.pathAvailable && (!deferred || in.replayAvailable) &&
        out.deadlineSlackSeconds >= 0)
    {
        for (uint32_t d = 10; d <= 100; ++d)
            for (uint32_t n = 1; n <= 100 && n * d <= 1000; ++n)
            {
                const double delta = d / 1000.0;
                const double recovery =
                    in.variableBytes * (n - 1) * delta / (2 * in.backupBandwidth) +
                    cR * (n - 1) / n + in.work * delta / (2 * in.recoveryRate);
                if (!std::isfinite(recovery))
                    throw std::invalid_argument("frequency recovery estimate overflow");
                if (faultInput + recovery > out.deadlineSlackSeconds)
                {
                    ++out.deadlineRejected;
                    continue;
                }
                const FrequencyConfiguration config{d, n};
                const auto demand = in.storageDemand(config);
                if (!demand || demand->localAdditionalBytes > in.localFreeBytes ||
                    demand->remoteAdditionalBytes > in.remoteFreeBytes)
                {
                    ++out.storageRejected;
                    continue;
                }
                ++out.feasibleCount;
                const double maintenance = cL / delta + cR / (n * delta);
                // Trem * (muP/W) = 1-x in the analytical continuous-work model.
                const double normal = on ? (interval * in.primaryRate / in.work) * maintenance
                                         : (1 - in.progress) * maintenance;
                const double objective =
                    normal + (on ? in.risk.qCurrentSample : *out.pFailAfterInitReady) * recovery;
                if (!std::isfinite(objective))
                    throw std::invalid_argument("frequency objective overflow");
                const FrequencyCandidate candidate{config, objective, recovery, normal, *demand};
                if (!out.selected || FrequencyCandidateLess(candidate, *out.selected))
                    out.selected = candidate;
            }
    }
    if (!out.selected)
    {
        out.action = on ? FrequencyAction::PAUSE : FrequencyAction::NONE;
        out.reason = !in.nodeAvailable || !in.pathAvailable || (deferred && !in.replayAvailable)
                         ? "PLACEMENT_UNAVAILABLE"
                     : out.deadlineSlackSeconds < 0         ? "DEADLINE_INFEASIBLE"
                     : out.storageRejected                  ? "STORAGE_INFEASIBLE"
                                                            : "DEADLINE_INFEASIBLE";
        return out;
    }
    if (on)
    {
        out.action = FrequencyAction::UPDATE;
        out.reason = "ON_MINIMUM_SCORE";
        return out;
    }
    // Pre-ready faults have no checkpoint benefit. Deferred INPUT is common to both
    // alternatives and omitted from both scores, but remains in hard feasibility.
    out.jStart = cL + cR + out.selected->objective;
    if (out.initializationSeconds >= in.remainingSeconds)
        out.reason = "INITIALIZATION_TOO_LATE";
    else if (*out.pFailAfterInitReady == 0)
        out.reason = "NO_FAULT_AFTER_INIT_READY";
    else if ((!in.replayAvailable && *out.pFailAfterInitReady > 0) ||
             (out.jOff && *out.jStart < *out.jOff))
    {
        out.action = FrequencyAction::START;
        out.reason = "START_BENEFICIAL";
    }
    else
        out.reason = "OFF_NOT_MORE_EXPENSIVE";
    return out;
}
} // namespace ns3::protection
