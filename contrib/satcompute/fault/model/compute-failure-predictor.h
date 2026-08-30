/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_FAILURE_PREDICTOR_H
#define SATCOMPUTE_COMPUTE_FAILURE_PREDICTOR_H

#include <cstdint>

namespace ns3
{

/** One causal probability forecast for a currently running compute task. */
struct ComputeFailurePrediction
{
    double combinedStepFailureProbability{}; ///< Current q_comp in [0, 1].
    uint64_t horizonStepCount{}; ///< Checks from now through task completion.
    double predictedFailureProbability{}; ///< P(failure before task completion).
};

/**
 * Accumulate a stationary per-check failure probability across a horizon.
 *
 * The current check is included, so a running task always has at least one
 * opportunity to fail even when its completion event shares this timestamp.
 * This is a causal baseline: it freezes the probability visible now and does
 * not inspect future fault events or the realized risk duration.
 *
 * @param combinedStepFailureProbability Current q_comp in [0, 1].
 * @param remainingComputeTimeNs Known non-negative time until compute completion.
 * @param checkIntervalNs Positive fault-model check interval.
 * @return Current probability, horizon length, and cumulative prediction.
 */
ComputeFailurePrediction PredictComputeFailureBeforeFinish(
    double combinedStepFailureProbability,
    int64_t remainingComputeTimeNs,
    int64_t checkIntervalNs);

/**
 * Combine independent F1/F2 probabilities before forecasting the task horizon.
 *
 * @param f1StepFailureProbability Current F1 probability in [0, 1].
 * @param f2StepFailureProbability Current F2 probability in [0, 1].
 * @param remainingComputeTimeNs Known non-negative time until compute completion.
 * @param checkIntervalNs Positive fault-model check interval.
 * @return Combined current probability and cumulative task-window prediction.
 */
ComputeFailurePrediction PredictComputeFailureBeforeFinish(
    double f1StepFailureProbability,
    double f2StepFailureProbability,
    int64_t remainingComputeTimeNs,
    int64_t checkIntervalNs);

} // namespace ns3

#endif // SATCOMPUTE_COMPUTE_FAILURE_PREDICTOR_H
