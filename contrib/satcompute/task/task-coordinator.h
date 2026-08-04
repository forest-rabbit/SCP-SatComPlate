/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TASK_COORDINATOR_H
#define SATCOMPUTE_TASK_COORDINATOR_H

#include "compute-profile.h"
#include "compute-service.h"
#include "compute-task.h"
#include "task-trace.h"

#include "../topology/satellite-runtime-view.h"
#include "../traffic/network-transfer-engine.h"

#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

struct TaskEventRecord
{
    int64_t simulationTimeNs{};
    uint64_t taskId{};
    TaskState fromState{TASK_PENDING};
    TaskState toState{TASK_PENDING};
    uint32_t nodeId{};
    std::string cause;
};

/** Coordinate input transfer, FCFS compute, and result transfer lifecycles. */
class TaskCoordinator : public Object
{
  public:
    static TypeId GetTypeId();

    TaskCoordinator();
    ~TaskCoordinator() override;

    void Initialize(const ComputeProfile& computeProfile,
                    const TaskTrace& taskTrace,
                    SatelliteRuntimeView& topology,
                    const std::string& transferChunkMode,
                    uint32_t transferPayloadBytes,
                    uint16_t islMtuBytes,
                    uint32_t receiverRcvBufBytes,
                    bool collectUdpSocketDrops,
                    int64_t simulationDurationNs);

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
    void HandleInputTransferComplete(uint64_t transferId, int64_t completionTimeNs);
    void HandleComputeStart(uint64_t taskId, uint32_t nodeId, int64_t startTimeNs);
    void HandleComputeComplete(uint64_t taskId, uint32_t nodeId, int64_t completionTimeNs);
    void HandleResultTransferComplete(uint64_t transferId, int64_t completionTimeNs);

    bool m_initialized{};
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
