/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// 定义任务静态字段、六状态运行时模型及严格的状态转换检查。

#include "compute-task.h"

#include "ns3/abort.h"

namespace ns3 {

namespace {

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
    }
  return "UNKNOWN";
}

TaskDefinition::TaskDefinition()
  : taskId(0),
    sourceNodeId(0),
    computeNodeId(0),
    resultNodeId(0),
    inputBytes(0),
    outputBytes(0),
    computeWorkUnits(0),
    arrivalTimeNs(0),
    inputTransferId(0),
    resultTransferId(0)
{
}

TaskRuntime::TaskRuntime(const TaskDefinition& taskDefinition)
  : definition(taskDefinition),
    state(TASK_PENDING),
    lastTransitionTimeNs(-1),
    inputTransferCompleteTimeNs(-1),
    queueEnterTimeNs(-1),
    computeStartTimeNs(-1),
    computeCompleteTimeNs(-1),
    resultTransferStartTimeNs(-1),
    resultTransferCompleteTimeNs(-1)
{
  NS_ABORT_MSG_IF(definition.taskId == 0,
                  "TaskRuntime 要求正 task_id");
}

void
TaskRuntime::TransitionTo(TaskState requestedState,
                          int64_t eventTimeNs,
                          const std::string& cause)
{
  NS_ABORT_MSG_IF(
    !IsNextTaskState(state, requestedState),
    "非法任务状态转换: task_id=" << definition.taskId
      << " current_state=" << TaskStateToString(state)
      << " requested_state=" << TaskStateToString(requestedState)
      << " event_time_ns=" << eventTimeNs
      << " cause=" << cause);
  NS_ABORT_MSG_IF(
    eventTimeNs < 0
      || (lastTransitionTimeNs >= 0 && eventTimeNs < lastTransitionTimeNs),
    "任务状态转换时间无效: task_id=" << definition.taskId
      << " current_state=" << TaskStateToString(state)
      << " requested_state=" << TaskStateToString(requestedState)
      << " event_time_ns=" << eventTimeNs
      << " cause=" << cause);
  NS_ABORT_MSG_IF(
    state == TASK_PENDING && eventTimeNs != definition.arrivalTimeNs,
    "TASK_ARRIVAL 时间必须等于 arrival_time_ns: task_id="
      << definition.taskId
      << " arrival_time_ns=" << definition.arrivalTimeNs
      << " event_time_ns=" << eventTimeNs
      << " cause=" << cause);
  NS_ABORT_MSG_IF(cause.empty(),
                  "任务状态转换 cause 不能为空: task_id="
                    << definition.taskId);

  switch (requestedState)
    {
    case TASK_INPUT_TRANSFERRING:
      break;
    case TASK_QUEUED:
      inputTransferCompleteTimeNs = eventTimeNs;
      queueEnterTimeNs = eventTimeNs;
      break;
    case TASK_RUNNING:
      computeStartTimeNs = eventTimeNs;
      break;
    case TASK_RESULT_TRANSFERRING:
      computeCompleteTimeNs = eventTimeNs;
      resultTransferStartTimeNs = eventTimeNs;
      break;
    case TASK_COMPLETED:
      resultTransferCompleteTimeNs = eventTimeNs;
      break;
    case TASK_PENDING:
      NS_ABORT_MSG("不能转换回 PENDING: task_id=" << definition.taskId);
      break;
    }

  state = requestedState;
  lastTransitionTimeNs = eventTimeNs;
}

} // namespace ns3
