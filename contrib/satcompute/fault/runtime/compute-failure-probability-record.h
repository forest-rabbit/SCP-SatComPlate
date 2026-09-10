/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_FAILURE_PROBABILITY_RECORD_H
#define SATCOMPUTE_COMPUTE_FAILURE_PROBABILITY_RECORD_H

#include <cstdint>

namespace ns3
{

/** One model-driven compute-failure probability observation for a running task. */
struct ComputeFailureProbabilityRecord
{
    int64_t simulationTimeNs{}; ///< Time represented by this observation.
    uint32_t nodeId{}; ///< Stable compute-satellite ID.
    uint64_t taskId{}; ///< Stable running-task ID.
    int64_t taskComputeStartTimeNs{}; ///< Already observed compute-dispatch time.
    int64_t taskServiceTimeNs{}; ///< Fixed task compute duration.
    int64_t taskElapsedTimeNs{}; ///< Known compute progress in nanoseconds.
    int64_t remainingComputeTimeNs{}; ///< Known time to scheduled completion.
    int64_t expectedComputeCompletionTimeNs{}; ///< Known scheduled completion time.
    double completionRatio{}; ///< Known task completion ratio in [0, 1].
    double f1StepFailureProbability{}; ///< Current conditional F1 probability.
    double f2StepFailureProbability{}; ///< Current conditional F2 probability.
    double combinedStepFailureProbability{}; ///< Current q_comp union probability.
    uint64_t horizonStepCount{}; ///< Current/future checks through completion.
    double failureBeforeFinishProbability{}; ///< P(F1 or F2 before completion).
};

} // namespace ns3

#endif // SATCOMPUTE_COMPUTE_FAILURE_PROBABILITY_RECORD_H
