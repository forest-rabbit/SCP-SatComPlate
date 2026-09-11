/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-frequency-policy.h"

#include <algorithm>
#include <cmath>
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
} // namespace

FrequencyRisk MakeFrequencyRisk(double currentSamplerQ, const ComputeFailurePredictionInput& in)
{
    Probability(currentSamplerQ);
    const auto prediction = PredictComputeFailureBeforeFinish(in);
    if (currentSamplerQ != prediction.combinedStepFailureProbability)
        throw std::invalid_argument("policy q differs from current sampler q");
    return {in.predictionTimeNs,
            in.checkIntervalNs,
            currentSamplerQ,
            prediction.predictedFailureProbability};
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
    const bool deferred = in.inputPolicy == InputStagingPolicy::DEFERRED;
    const double faultInput = deferred && in.replayAvailable ? in.inputBytes / in.inputBandwidth : 0;
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
    if (in.progress >= 1 || in.remainingSeconds <= 0)
    {
        out.reason = "NO_REMAINING_COMPUTE";
        return out;
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
                    normal + (on ? in.risk.qCurrentSample : in.risk.pFailBeforeFinish) * recovery;
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
    // Deferred START and OFF both pay one original INPUT after a fault.
    // It is constant across frequency candidates, but must not bias START vs OFF.
    out.jStart = cL + cR + out.selected->objective + in.risk.pFailBeforeFinish * faultInput;
    if (out.initializationSeconds >= in.remainingSeconds)
        out.reason = "INITIALIZATION_TOO_LATE";
    else if ((!in.replayAvailable && in.risk.pFailBeforeFinish > 0) ||
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
