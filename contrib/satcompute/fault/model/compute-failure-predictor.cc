/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "compute-failure-predictor.h"

#include "compute-fault-combination.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace ns3
{

ComputeFailurePrediction
PredictComputeFailureBeforeFinish(const ComputeFailurePredictionInput& input)
{
    if (input.f1Model == nullptr && input.f2Model == nullptr)
    {
        throw std::invalid_argument(
            "compute failure prediction requires F1 or F2");
    }
    if (input.predictionTimeNs < 0 || input.remainingComputeTimeNs < 0)
    {
        throw std::invalid_argument(
            "compute failure prediction times must be non-negative");
    }
    if (input.checkIntervalNs <= 0)
    {
        throw std::invalid_argument("fault check interval must be positive");
    }
    if (input.remainingComputeTimeNs >
        std::numeric_limits<int64_t>::max() - input.predictionTimeNs)
    {
        throw std::invalid_argument(
            "compute failure prediction horizon overflows int64");
    }
    if (input.f2Model != nullptr && !input.f2PositionAtTime)
    {
        throw std::invalid_argument(
            "F2 compute failure prediction requires future positions");
    }

    const double intervalSeconds =
        static_cast<double>(input.checkIntervalNs) / 1000000000.0;
    F1SelfStateFaultSnapshot f1State = input.f1State;
    F2RadiationFaultSnapshot f2State = input.f2State;
    const uint64_t futureStepCount =
        static_cast<uint64_t>(input.remainingComputeTimeNs) /
        static_cast<uint64_t>(input.checkIntervalNs);

    ComputeFailurePrediction prediction;
    prediction.steps.reserve(static_cast<std::size_t>(futureStepCount + 1));
    double logSurvivalProbability = 0.0;
    bool certainFailure = false;
    for (uint64_t stepIndex = 0; stepIndex <= futureStepCount; ++stepIndex)
    {
        const int64_t targetTimeNs =
            input.predictionTimeNs +
            static_cast<int64_t>(stepIndex *
                                 static_cast<uint64_t>(input.checkIntervalNs));
        if (stepIndex > 0)
        {
            if (input.f1Model != nullptr)
            {
                input.f1Model->Update(f1State, true, intervalSeconds, intervalSeconds);
            }
            if (input.f2Model != nullptr)
            {
                input.f2Model->Update(f2State,
                                      input.f2PositionAtTime(targetTimeNs),
                                      intervalSeconds);
            }
        }
        const double f1Probability =
            input.f1Model != nullptr ? f1State.stepFailureProbability : 0.0;
        const double f2Probability =
            input.f2Model != nullptr ? f2State.stepFailureProbability : 0.0;
        const double combinedProbability =
            CombineComputeFaultProbabilities(f1Probability, f2Probability);
        prediction.steps.push_back(
            {targetTimeNs, f1Probability, f2Probability, combinedProbability});
        if (combinedProbability == 1.0)
        {
            certainFailure = true;
        }
        else if (!certainFailure)
        {
            logSurvivalProbability += std::log1p(-combinedProbability);
        }
    }
    prediction.f1StepFailureProbability =
        prediction.steps.front().f1StepFailureProbability;
    prediction.f2StepFailureProbability =
        prediction.steps.front().f2StepFailureProbability;
    prediction.combinedStepFailureProbability =
        prediction.steps.front().combinedStepFailureProbability;
    prediction.horizonStepCount = prediction.steps.size();
    prediction.predictedFailureProbability =
        certainFailure ? 1.0 : -std::expm1(logSurvivalProbability);
    return prediction;
}

} // namespace ns3
