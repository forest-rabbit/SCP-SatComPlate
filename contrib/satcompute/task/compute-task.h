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

#ifndef SATCOMPUTE_COMPUTE_TASK_H
#define SATCOMPUTE_COMPUTE_TASK_H

#include <cstdint>
#include <string>

namespace ns3 {

enum TaskState
{
  TASK_PENDING,
  TASK_INPUT_TRANSFERRING,
  TASK_QUEUED,
  TASK_RUNNING,
  TASK_RESULT_TRANSFERRING,
  TASK_COMPLETED
};

const char* TaskStateToString(TaskState state);

struct TaskDefinition
{
  uint64_t taskId;
  uint32_t sourceNodeId;
  uint32_t computeNodeId;
  uint32_t resultNodeId;
  uint64_t inputBytes;
  uint64_t outputBytes;
  uint64_t computeWorkUnits;
  int64_t arrivalTimeNs;
  uint64_t inputTransferId;
  uint64_t resultTransferId;

  TaskDefinition();
};

struct TaskRuntime
{
  explicit TaskRuntime(const TaskDefinition& taskDefinition);

  void TransitionTo(TaskState requestedState,
                    int64_t eventTimeNs,
                    const std::string& cause);

  TaskDefinition definition;
  TaskState state;
  int64_t lastTransitionTimeNs;
  int64_t inputTransferCompleteTimeNs;
  int64_t queueEnterTimeNs;
  int64_t computeStartTimeNs;
  int64_t computeCompleteTimeNs;
  int64_t resultTransferStartTimeNs;
  int64_t resultTransferCompleteTimeNs;
};

} // namespace ns3

#endif
