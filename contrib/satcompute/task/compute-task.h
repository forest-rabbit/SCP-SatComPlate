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
    TASK_FAILED
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
    RESULT_TRANSFER_FAILED
};

const char* TaskFailureReasonToString(TaskFailureReason reason);

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
};

struct TaskRuntime
{
    explicit TaskRuntime(const TaskDefinition& taskDefinition);

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
    int64_t computeCompleteTimeNs{-1};
    int64_t resultTransferStartTimeNs{-1};
    int64_t resultTransferCompleteTimeNs{-1};
    int64_t failureTimeNs{-1};
    TaskFailureReason failureReason{TaskFailureReason::NONE};
};

} // namespace ns3

#endif
