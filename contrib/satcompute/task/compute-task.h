/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_TASK_H
#define SATCOMPUTE_COMPUTE_TASK_H

#include <cstdint>
#include <string>

namespace ns3
{

enum TaskState
{
    TASK_PENDING,
    TASK_INPUT_TRANSFERRING,
    TASK_QUEUED,
    TASK_RUNNING,
    TASK_RESULT_TRANSFERRING,
    TASK_COMPLETED,
    TASK_FAILED,
    TASK_RECOVERING,
    TASK_RUNNING_BACKUP
};

const char* TaskStateToString(TaskState state);
bool IsTerminalTaskState(TaskState state);

enum class TaskFailureReason
{
    NONE,
    COMPUTE_NODE_FAILURE,
    SOURCE_SATELLITE_FAILURE,
    COMPUTE_SATELLITE_FAILURE,
    RESULT_SATELLITE_FAILURE,
    INPUT_TRANSFER_FAILED,
    RESULT_TRANSFER_FAILED,
    COMPUTE_DEADLINE_EXCEEDED,
    SIMULATION_ENDED
};

const char* TaskFailureReasonToString(TaskFailureReason reason);

/** Closed-world task classes; UNSPECIFIED is only for legacy inputs. */
enum class TaskProfile
{
    UNSPECIFIED,
    DENSE_IMAGE,
    SPARSE_INFERENCE,
    COMPRESSION,
    LLM
};

const char* TaskProfileToString(TaskProfile profile);

/** Ceiling of baseline ns times the shortest decimal representation of factor. */
int64_t CalculateComputeDeadlineBudgetNs(int64_t baselineTimeNs, double factor);

struct TaskDefinition
{
    uint64_t taskId{};
    uint32_t sourceNodeId{};
    uint32_t computeNodeId{};
    uint32_t resultNodeId{};
    uint64_t inputBytes{};
    uint64_t outputBytes{};
    uint64_t computeWorkUnits{};
    int64_t arrivalTimeNs{};
    uint64_t inputTransferId{};
    uint64_t resultTransferId{};
    TaskProfile taskProfile{TaskProfile::UNSPECIFIED};
};

struct TaskRuntime
{
    explicit TaskRuntime(const TaskDefinition& taskDefinition);

    void ConfigureComputeDeadline(uint64_t referenceRate, double factor);
    bool EstablishComputeDeadline(int64_t firstStartTimeNs);
    bool ComputeDeadlineMet() const;
    bool ResultDelivered() const;
    bool TaskSucceeded() const;

    void TransitionTo(TaskState requestedState,
                      int64_t eventTimeNs,
                      const std::string& cause);
    bool FailIfActive(int64_t eventTimeNs,
                      TaskFailureReason reason,
                      const std::string& cause);

    TaskDefinition definition;
    TaskState state{TASK_PENDING};
    int64_t lastTransitionTimeNs{-1};
    int64_t inputTransferCompleteTimeNs{-1};
    int64_t queueEnterTimeNs{-1};
    int64_t computeStartTimeNs{-1};
    int64_t baselineComputeTimeNs{-1};
    int64_t computeDeadlineBudgetNs{-1};
    int64_t computeDeadlineTimeNs{-1};
    int64_t computeCompleteTimeNs{-1};
    int64_t resultTransferStartTimeNs{-1};
    int64_t resultTransferCompleteTimeNs{-1};
    int64_t failureTimeNs{-1};
    TaskFailureReason failureReason{TaskFailureReason::NONE};
    uint64_t attemptGeneration{};       ///< Primary 0, sole recovery 1; never reset.
    bool parallelExecution{};          ///< Independent attempts share one logical outcome.
    uint32_t activeComputeNodeId{};     ///< Actual executing node, not immutable placement.
    uint64_t winningResultTransferId{}; ///< Zero only for a local recovery result.
    bool localResultDelivered{};        ///< Logical bytes delivered without a network flow.
    uint64_t actualComputeServiceNs{}; ///< Sum of interrupted and winning service, excluding waits.
};

} // namespace ns3

#endif
