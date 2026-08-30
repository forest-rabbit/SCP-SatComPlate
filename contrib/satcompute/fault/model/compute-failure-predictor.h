/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_FAILURE_PREDICTOR_H
#define SATCOMPUTE_COMPUTE_FAILURE_PREDICTOR_H

#include "ns3/f1-self-state-fault-model.h"
#include "ns3/f2-radiation-fault-model.h"
#include "ns3/vector.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ns3
{

/** Provide one deterministic future ECEF position at an absolute time. */
using F2FuturePositionProvider = std::function<Vector(int64_t)>;

/** One predicted F1/F2 probability tuple at a future fault check. */
struct ComputeFailurePredictionStep
{
    int64_t targetTimeNs{}; ///< Absolute fault-check time represented by this step.
    double f1StepFailureProbability{}; ///< Predicted conditional F1 probability.
    double f2StepFailureProbability{}; ///< Predicted conditional F2 probability.
    double combinedStepFailureProbability{}; ///< Predicted union probability.
};

/** Causal model state and task horizon used by one rolling prediction. */
struct ComputeFailurePredictionInput
{
    const F1SelfStateFaultModel* f1Model{}; ///< Shared immutable F1 model, or null.
    F1SelfStateFaultSnapshot f1State; ///< Private F1 state copy at prediction time.
    const F2RadiationFaultModel* f2Model{}; ///< Shared immutable F2 model, or null.
    F2RadiationFaultSnapshot f2State; ///< Private F2 state copy at prediction time.
    F2FuturePositionProvider f2PositionAtTime; ///< Native future orbit query for F2.
    int64_t predictionTimeNs{}; ///< Absolute time of the current model check.
    int64_t remainingComputeTimeNs{}; ///< Time until current task completion.
    int64_t checkIntervalNs{}; ///< Positive F1/F2 update and sampling interval.
};

/** One complete task-window prediction and its auditable step trajectory. */
struct ComputeFailurePrediction
{
    std::vector<ComputeFailurePredictionStep> steps; ///< Current and future checks.
    double f1StepFailureProbability{}; ///< Current F1 conditional probability.
    double f2StepFailureProbability{}; ///< Current F2 conditional probability.
    double combinedStepFailureProbability{}; ///< Current one-step union probability.
    uint64_t horizonStepCount{}; ///< Number of current/future checks in steps.
    double predictedFailureProbability{}; ///< P(any compute fault before completion).
};

/**
 * Forecast F1/F2 probabilities under conditional task survival.
 *
 * The current model check is included. Future F1 state assumes the current task
 * remains busy until completion; future F2 state uses the supplied native orbit
 * positions. Model state is copied, no random stream is consumed, and no live
 * simulation object is mutated.
 *
 * @param input Current model snapshots, task horizon, and future orbit provider.
 * @return Per-check probability trajectory and cumulative union probability.
 * @throws std::invalid_argument if the horizon or required model input is invalid.
 */
ComputeFailurePrediction PredictComputeFailureBeforeFinish(
    const ComputeFailurePredictionInput& input);

/**
 * Transitional stationary-probability overload used by the old runtime slice.
 *
 * This overload is removed when the runtime engine starts passing shared F1/F2
 * model state. New code must use ComputeFailurePredictionInput.
 *
 * @param combinedStepFailureProbability Current one-step union probability.
 * @param remainingComputeTimeNs Time until current task completion.
 * @param checkIntervalNs Positive model check interval.
 * @return A stationary compatibility forecast.
 */
ComputeFailurePrediction PredictComputeFailureBeforeFinish(
    double combinedStepFailureProbability,
    int64_t remainingComputeTimeNs,
    int64_t checkIntervalNs);

} // namespace ns3

#endif // SATCOMPUTE_COMPUTE_FAILURE_PREDICTOR_H
