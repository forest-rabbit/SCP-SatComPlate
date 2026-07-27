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

#ifndef SATCOMPUTE_TASK_COORDINATOR_H
#define SATCOMPUTE_TASK_COORDINATOR_H

#include "compute-profile.h"
#include "compute-service.h"
#include "compute-task.h"
#include "task-trace.h"

#include "../topo.h"
#include "../traffic/network-transfer-engine.h"

#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ns3 {

struct TaskEventRecord
{
  int64_t simulationTimeNs;
  uint64_t taskId;
  TaskState fromState;
  TaskState toState;
  uint32_t nodeId;
  std::string cause;
};

class TaskCoordinator : public Object
{
public:
  static TypeId GetTypeId();

  TaskCoordinator();
  ~TaskCoordinator() override;

  void Initialize(const ComputeProfile& computeProfile,
                  const TaskTrace& taskTrace,
                  const SatelliteTopology& topology,
                  const std::string& transferChunkMode,
                  uint32_t transferPayloadBytes,
                  uint16_t islMtuBytes,
                  uint32_t receiverRcvBufBytes,
                  bool collectUdpSocketDrops,
                  double simulationDurationSeconds,
                  const std::string& taskLogMode);
  bool IsComplete() const;
  void ValidateCompleted() const;

  Ptr<NetworkTransferEngine> GetTransferEngine() const;
  const std::vector<TaskRuntime>& GetTaskRuntimes() const;
  const std::vector<Ptr<ComputeService>>& GetComputeServices() const;
  const std::vector<TaskEventRecord>& GetTaskEvents() const;

private:
  uint32_t GetTaskIndex(uint64_t taskId) const;
  TaskRuntime& GetTask(uint64_t taskId);
  const TaskRuntime& GetTask(uint64_t taskId) const;
  Ptr<ComputeService> GetComputeService(uint32_t nodeId) const;
  void TransitionTask(uint64_t taskId,
                      TaskState requestedState,
                      uint32_t nodeId,
                      int64_t eventTimeNs,
                      const std::string& cause);
  void HandleTaskArrival(uint64_t taskId);
  void HandleInputTransferComplete(uint64_t transferId,
                                   int64_t completionTimeNs);
  void HandleComputeStart(uint64_t taskId,
                          uint32_t nodeId,
                          int64_t startTimeNs);
  void HandleComputeComplete(uint64_t taskId,
                             uint32_t nodeId,
                             int64_t completionTimeNs);
  void HandleResultTransferComplete(uint64_t transferId,
                                    int64_t completionTimeNs);

  bool m_initialized;
  std::string m_taskLogMode;
  std::vector<TaskRuntime> m_tasks;
  std::map<uint64_t, uint32_t> m_taskIndexes;
  std::vector<Ptr<ComputeService>> m_computeServices;
  std::map<uint32_t, Ptr<ComputeService>> m_servicesByNodeId;
  std::map<uint64_t, uint64_t> m_inputTransferTasks;
  std::map<uint64_t, uint64_t> m_resultTransferTasks;
  std::vector<TaskEventRecord> m_taskEvents;
  Ptr<NetworkTransferEngine> m_transferEngine;
};

} // namespace ns3

#endif
