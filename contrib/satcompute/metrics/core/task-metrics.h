/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TASK_METRICS_H
#define SATCOMPUTE_TASK_METRICS_H

#include <cstdint>
#include <string>

namespace ns3
{

class TaskCoordinator;

/** Task and compute-node counters aggregated across one run. */
struct TaskAggregate
{
    uint64_t computeNodeCount{};
    uint64_t taskCount{};
    uint64_t completedTaskCount{};
    uint64_t totalInputBytes{};
    uint64_t totalOutputBytes{};
    uint64_t totalComputeWorkUnits{};
    uint64_t totalCompletionDelayNs{};
    uint64_t meanCompletionDelayNs{};
    uint64_t maxCompletionDelayNs{};
};

TaskAggregate CollectTaskAggregate(const TaskCoordinator* coordinator);

/** Legacy seconds-based entry point retained for ns-3.33 call-site parity. */
void WriteTaskMetrics(const TaskCoordinator& coordinator,
                      double simulationDurationSeconds,
                      const std::string& outputDirectory);

/** Precise internal entry point using the resolved integer-nanosecond duration. */
void WriteTaskMetricsNs(const TaskCoordinator& coordinator,
                        int64_t simulationDurationNs,
                        const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_TASK_METRICS_H
