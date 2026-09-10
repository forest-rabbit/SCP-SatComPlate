/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Keep the task lifecycle linear and timestamp-monotonic.

#include "compute-task.h"
#include "compute-service.h"

#include "ns3/abort.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ns3
{

namespace
{

bool
IsNextTaskState(TaskState currentState, TaskState requestedState)
{
    switch (currentState)
    {
    case TASK_PENDING:
        return requestedState == TASK_INPUT_TRANSFERRING;
    case TASK_INPUT_TRANSFERRING:
        return requestedState == TASK_QUEUED;
    case TASK_QUEUED:
        return requestedState == TASK_RUNNING;
    case TASK_RUNNING:
        return requestedState == TASK_RESULT_TRANSFERRING;
    case TASK_RESULT_TRANSFERRING:
        return requestedState == TASK_COMPLETED;
    case TASK_COMPLETED:
    case TASK_FAILED:
        return false;
    }
    return false;
}

} // namespace

const char*
TaskStateToString(TaskState state)
{
    switch (state)
    {
    case TASK_PENDING:
        return "PENDING";
    case TASK_INPUT_TRANSFERRING:
        return "INPUT_TRANSFERRING";
    case TASK_QUEUED:
        return "QUEUED";
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_RESULT_TRANSFERRING:
        return "RESULT_TRANSFERRING";
    case TASK_COMPLETED:
        return "COMPLETED";
    case TASK_FAILED:
        return "FAILED";
    }
    return "UNKNOWN";
}

bool
IsTerminalTaskState(TaskState state)
{
    return state == TASK_COMPLETED || state == TASK_FAILED;
}

const char*
TaskFailureReasonToString(TaskFailureReason reason)
{
    switch (reason)
    {
    case TaskFailureReason::NONE:
        return "";
    case TaskFailureReason::COMPUTE_NODE_FAILURE:
        return "COMPUTE_NODE_FAILURE";
    case TaskFailureReason::SOURCE_SATELLITE_FAILURE:
        return "SOURCE_SATELLITE_FAILURE";
    case TaskFailureReason::COMPUTE_SATELLITE_FAILURE:
        return "COMPUTE_SATELLITE_FAILURE";
    case TaskFailureReason::RESULT_SATELLITE_FAILURE:
        return "RESULT_SATELLITE_FAILURE";
    case TaskFailureReason::INPUT_TRANSFER_FAILED:
        return "INPUT_TRANSFER_FAILED";
    case TaskFailureReason::RESULT_TRANSFER_FAILED:
        return "RESULT_TRANSFER_FAILED";
    case TaskFailureReason::COMPUTE_DEADLINE_EXCEEDED:
        return "COMPUTE_DEADLINE_EXCEEDED";
    }
    return "UNKNOWN";
}

TaskRuntime::TaskRuntime(const TaskDefinition& taskDefinition)
    : definition(taskDefinition)
{
    NS_ABORT_MSG_IF(definition.taskId == 0, "TaskRuntime requires a positive task ID");
}

int64_t
CalculateComputeDeadlineBudgetNs(int64_t baselineTimeNs, double factor)
{
    if (baselineTimeNs <= 0 || !std::isfinite(factor) || factor < 1)
    {
        throw std::invalid_argument("invalid compute deadline baseline/factor");
    }
    if (factor > static_cast<double>(std::numeric_limits<int64_t>::max()))
    {
        throw std::overflow_error("compute deadline budget exceeds int64 ns");
    }
    // Decimal 1.3 must not turn an exact 19.5 s budget into 19.5 s + 1 ns
    // merely because its binary floating-point representation is slightly larger.
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), factor);
    if (converted.ec != std::errc{})
    {
        throw std::invalid_argument("cannot represent compute deadline factor");
    }
    const std::string decimal(buffer, converted.ptr);
    const auto exponentAt = decimal.find_first_of("eE");
    const int exponent =
        exponentAt == std::string::npos ? 0 : std::stoi(decimal.substr(exponentAt + 1));
    unsigned __int128 numerator = 0;
    unsigned __int128 denominator = 1;
    int fractionalDigits = 0;
    bool fractional = false;
    for (char digit : decimal.substr(0, exponentAt))
    {
        if (digit == '.')
        {
            fractional = true;
            continue;
        }
        numerator = numerator * 10 + (digit - '0');
        fractionalDigits += fractional ? 1 : 0;
    }
    for (int i = 0; i < fractionalDigits - exponent; ++i)
        denominator *= 10;
    for (int i = 0; i < exponent - fractionalDigits; ++i)
        numerator *= 10;
    const auto scaled = static_cast<unsigned __int128>(baselineTimeNs) * numerator;
    const auto result = scaled / denominator + (scaled % denominator != 0);
    if (result > static_cast<unsigned __int128>(std::numeric_limits<int64_t>::max()))
    {
        throw std::overflow_error("compute deadline budget exceeds int64 ns");
    }
    return static_cast<int64_t>(result);
}

void
TaskRuntime::ConfigureComputeDeadline(uint64_t referenceRate, double factor)
{
    if (baselineComputeTimeNs >= 0)
    {
        throw std::logic_error("compute deadline is already configured");
    }
    baselineComputeTimeNs =
        ComputeService::CalculateServiceTimeNs(definition.computeWorkUnits, referenceRate);
    computeDeadlineBudgetNs = CalculateComputeDeadlineBudgetNs(baselineComputeTimeNs, factor);
}

bool
TaskRuntime::EstablishComputeDeadline(int64_t firstStartTimeNs)
{
    if (computeDeadlineTimeNs >= 0)
        return false;
    const int64_t start = computeStartTimeNs >= 0 ? computeStartTimeNs : firstStartTimeNs;
    if (firstStartTimeNs < 0 || computeDeadlineBudgetNs <= 0 ||
        start > std::numeric_limits<int64_t>::max() - computeDeadlineBudgetNs)
    {
        throw std::overflow_error("invalid absolute compute deadline");
    }
    if (computeStartTimeNs < 0)
        computeStartTimeNs = firstStartTimeNs;
    computeDeadlineTimeNs = computeStartTimeNs + computeDeadlineBudgetNs;
    return true;
}

bool
TaskRuntime::ComputeDeadlineMet() const
{
    return computeCompleteTimeNs >= 0 && computeDeadlineTimeNs >= 0 &&
           computeCompleteTimeNs <= computeDeadlineTimeNs;
}

bool
TaskRuntime::ResultDelivered() const
{
    return resultTransferCompleteTimeNs >= 0;
}

bool
TaskRuntime::TaskSucceeded() const
{
    return state == TASK_COMPLETED && ComputeDeadlineMet() && ResultDelivered();
}

const char*
TaskProfileToString(TaskProfile profile)
{
    switch (profile)
    {
    case TaskProfile::UNSPECIFIED:
        return "UNSPECIFIED";
    case TaskProfile::DENSE_IMAGE:
        return "dense-image";
    case TaskProfile::SPARSE_INFERENCE:
        return "sparse-inference";
    case TaskProfile::COMPRESSION:
        return "compression";
    case TaskProfile::LLM:
        return "llm";
    }
    NS_ABORT_MSG("unknown task profile");
    return "UNKNOWN";
}

void
TaskRuntime::TransitionTo(TaskState requestedState,
                          int64_t eventTimeNs,
                          const std::string& cause)
{
    NS_ABORT_MSG_IF(!IsNextTaskState(state, requestedState),
                    "invalid task-state transition for task_id=" << definition.taskId);
    NS_ABORT_MSG_IF(eventTimeNs < 0 ||
                        (lastTransitionTimeNs >= 0 && eventTimeNs < lastTransitionTimeNs),
                    "task-state transition time is not monotonic for task_id="
                        << definition.taskId);
    NS_ABORT_MSG_IF(state == TASK_PENDING && eventTimeNs != definition.arrivalTimeNs,
                    "first task transition must equal arrival_time_ns for task_id="
                        << definition.taskId);
    NS_ABORT_MSG_IF(cause.empty(), "task-state transition cause must not be empty");

    switch (requestedState)
    {
    case TASK_INPUT_TRANSFERRING:
        break;
    case TASK_QUEUED:
        inputTransferCompleteTimeNs = eventTimeNs;
        queueEnterTimeNs = eventTimeNs;
        break;
    case TASK_RUNNING:
        if (computeStartTimeNs < 0)
            computeStartTimeNs = eventTimeNs;
        if (computeDeadlineBudgetNs > 0)
            EstablishComputeDeadline(eventTimeNs);
        break;
    case TASK_RESULT_TRANSFERRING:
        computeCompleteTimeNs = eventTimeNs;
        resultTransferStartTimeNs = eventTimeNs;
        break;
    case TASK_COMPLETED:
        resultTransferCompleteTimeNs = eventTimeNs;
        break;
    case TASK_FAILED:
        NS_ABORT_MSG("TASK_FAILED requires FailIfActive");
    case TASK_PENDING:
        NS_ABORT_MSG("a task cannot transition back to PENDING");
    }
    state = requestedState;
    lastTransitionTimeNs = eventTimeNs;
}

bool
TaskRuntime::FailIfActive(int64_t eventTimeNs,
                          TaskFailureReason reason,
                          const std::string& cause)
{
    if (IsTerminalTaskState(state))
    {
        return false;
    }
    NS_ABORT_MSG_IF(eventTimeNs < definition.arrivalTimeNs ||
                        (lastTransitionTimeNs >= 0 && eventTimeNs < lastTransitionTimeNs),
                    "task failure time is invalid for task_id=" << definition.taskId);
    NS_ABORT_MSG_IF(reason == TaskFailureReason::NONE,
                    "task failure requires a concrete reason");
    NS_ABORT_MSG_IF(cause.empty(), "task failure cause must not be empty");

    state = TASK_FAILED;
    lastTransitionTimeNs = eventTimeNs;
    failureTimeNs = eventTimeNs;
    failureReason = reason;
    return true;
}

} // namespace ns3
