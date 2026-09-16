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
    const int64_t first = input.firstSampleTimeNs.value_or(input.predictionTimeNs);
    if (first < input.predictionTimeNs)
        throw std::invalid_argument("first prediction sample precedes snapshot");
    const int64_t finish = input.predictionTimeNs + input.remainingComputeTimeNs;

    ComputeFailurePrediction prediction;
    prediction.f1StepFailureProbability = input.f1Model ? f1State.stepFailureProbability : 0;
    prediction.f2StepFailureProbability = input.f2Model ? f2State.stepFailureProbability : 0;
    prediction.combinedStepFailureProbability =
        CombineComputeFaultProbabilities(prediction.f1StepFailureProbability,
                                         prediction.f2StepFailureProbability);
    double logSurvivalProbability = 0.0;
    bool certainFailure = false;
    int64_t previousTimeNs = input.predictionTimeNs;
    for (int64_t targetTimeNs = first;
         targetTimeNs < finish || (!input.finishExclusive && targetTimeNs == finish);)
    {
        if (targetTimeNs > previousTimeNs)
        {
            if (input.f1Model != nullptr)
            {
                input.f1Model->Update(f1State,
                                      true,
                                      static_cast<double>(targetTimeNs - previousTimeNs) / 1e9,
                                      intervalSeconds);
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
        previousTimeNs = targetTimeNs;
        if (input.checkIntervalNs > finish - targetTimeNs)
            break;
        targetTimeNs += input.checkIntervalNs;
    }
    prediction.horizonStepCount = prediction.steps.size();
    prediction.predictedFailureProbability =
        certainFailure ? 1.0 : -std::expm1(logSurvivalProbability);
    return prediction;
}

} // namespace ns3
