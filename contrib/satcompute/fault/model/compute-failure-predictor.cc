/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "compute-failure-predictor.h"

#include "compute-fault-combination.h"

#include <cmath>
#include <stdexcept>

namespace ns3
{

ComputeFailurePrediction
PredictComputeFailureBeforeFinish(double combinedStepFailureProbability,
                                  int64_t remainingComputeTimeNs,
                                  int64_t checkIntervalNs)
{
    // Reuse the shared probability validation without duplicating its contract.
    const double validatedProbability =
        CombineComputeFaultProbabilities(combinedStepFailureProbability, 0.0);
    if (remainingComputeTimeNs < 0)
    {
        throw std::invalid_argument(
            "remaining compute time must be non-negative");
    }
    if (checkIntervalNs <= 0)
    {
        throw std::invalid_argument("fault check interval must be positive");
    }

    const uint64_t remaining = static_cast<uint64_t>(remainingComputeTimeNs);
    const uint64_t interval = static_cast<uint64_t>(checkIntervalNs);
    uint64_t horizonStepCount = remaining / interval;
    if (remaining % interval != 0)
    {
        ++horizonStepCount;
    }
    if (horizonStepCount == 0)
    {
        horizonStepCount = 1;
    }

    double predictedFailureProbability = 0.0;
    if (horizonStepCount == 1 || validatedProbability == 1.0)
    {
        predictedFailureProbability = validatedProbability;
    }
    else if (validatedProbability > 0.0)
    {
        predictedFailureProbability =
            -std::expm1(static_cast<double>(horizonStepCount) *
                        std::log1p(-validatedProbability));
    }
    return {validatedProbability,
            horizonStepCount,
            predictedFailureProbability};
}

ComputeFailurePrediction
PredictComputeFailureBeforeFinish(double f1StepFailureProbability,
                                  double f2StepFailureProbability,
                                  int64_t remainingComputeTimeNs,
                                  int64_t checkIntervalNs)
{
    return PredictComputeFailureBeforeFinish(
        CombineComputeFaultProbabilities(f1StepFailureProbability,
                                         f2StepFailureProbability),
        remainingComputeTimeNs,
        checkIntervalNs);
}

} // namespace ns3
