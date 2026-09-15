/* SPDX-License-Identifier: GPL-2.0-only */
#include "input-admission-policy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
InputAdmissionDecision EvaluateSelectiveInputAdmission(const InputAdmissionInput& in)
{
    InputAdmissionDecision out;
    if (in.startNs < 0 || in.remainingNs < 0 ||
        in.remainingNs > std::numeric_limits<int64_t>::max() - in.startNs)
        throw std::invalid_argument("invalid INPUT prediction horizon");
    if (!in.prediction) { out.reason = "PREDICTION_UNAVAILABLE"; return out; }
    const auto duration = in.path.TransferTimeNs(in.bytes);
    if (!duration) { out.reason = "PATH_UNAVAILABLE"; return out; }
    // LocalDelivery is not a serialization comparison or a fabricated network flow.
    if (in.path.local || !in.bytes) { out.send = true; out.reason = "LOCAL_DELIVERY"; return out; }
    out.networkReadyNs = *duration;
    out.serializationNs = *duration - in.path.propagationNs;
    const auto p = in.prediction->predictedFailureProbability;
    if (!std::isfinite(p) || p < 0 || p > 1)
        throw std::invalid_argument("invalid canonical INPUT P_F");
    long double survival = 1, mass = 0;
    int64_t previous = -1;
    const auto finish = in.startNs + in.remainingNs;
    for (const auto& step : in.prediction->steps)
    {
        const auto t = step.targetTimeNs;
        const auto q = step.combinedStepFailureProbability;
        if (!std::isfinite(q) || q < 0 || q > 1 || t < in.startNs ||
            t < in.firstSampleNs || t <= previous || t > finish || (in.finishExclusive && t == finish))
            throw std::invalid_argument("invalid canonical INPUT probability trajectory");
        previous = t;
        const auto w = survival * q;
        survival *= 1 - static_cast<long double>(q);
        mass += w;
        const auto lead = t - in.startNs;
        out.serialGainNs += w * std::min(out.serializationNs, lead);
        out.networkGainNs += w * std::min(out.networkReadyNs, lead);
    }
    // Representation validation only. NEVER a decision epsilon or renormalization.
    if (std::abs(mass - p) > 1e-12L)
        throw std::invalid_argument("INPUT trajectory does not cover canonical P_F");
    out.costNs = (1 - static_cast<long double>(p)) * out.serializationNs;
    out.send = out.serialGainNs > out.costNs;
    out.reason = out.send ? "POSITIVE_BREAK_EVEN" : "NONPOSITIVE_BREAK_EVEN";
    return out;
}
} // namespace ns3::protection
