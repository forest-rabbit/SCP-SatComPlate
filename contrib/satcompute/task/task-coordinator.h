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
#include "ns3/fault-definition.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
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

struct TaskFaultImpact
{
    uint64_t affectedTaskCount{};
    uint64_t affectedTransferCount{};
};

enum class TaskFaultKind
{
    COMPUTE,
    SATELLITE
};

struct TaskFaultNodeChange
{
    uint32_t nodeId{};
    TaskFaultKind kind{TaskFaultKind::COMPUTE};
    FaultDefinition fault; ///< Observed fault identity; zero ID only for legacy test helpers.
};

/** Immutable task state captured when a fault has an observable impact. */
struct FaultTaskImpactRecord
{
    FaultDefinition fault; ///< Observed START, duration and source hits.
    uint64_t taskId{}; ///< Stable task identity.
    int64_t impactTimeNs{}; ///< Observation time, possibly later than START.
    TaskState stateBeforeImpact{TASK_PENDING}; ///< State before this observation.
    std::string impactType; ///< Direct interruption or indirect outage exposure.
    int64_t computeStartTimeNs{-1}; ///< First compute start known at impact time.
    int64_t deadlineTimeNs{-1}; ///< Deadline known at impact time; never backfilled.
    bool progressValid{}; ///< True only for the observed running task.
    uint64_t completedWorkUnits{}; ///< Actual service work, not elapsed wall-clock fraction.
    uint64_t remainingWorkUnits{}; ///< Total work minus completed work when valid.
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
                    int64_t simulationDurationNs,
                    double computeDeadlineFactor = 1.3);

    bool IsComplete() const;
    /** Explicit fixed-runtime finalizer; leaves legacy off-mode truncation semantics unchanged. */
    void FinalizeSimulation();
    void ValidateCompleted() const;
    Ptr<NetworkTransferEngine> GetTransferEngine() const;
    const std::vector<TaskRuntime>& GetTaskRuntimes() const;
    const std::vector<Ptr<ComputeService>>& GetComputeServices() const;
    const std::vector<TaskEventRecord>& GetTaskEvents() const;
    /** @return Causal fault/task observations, independent of probability audit. */
    const std::vector<FaultTaskImpactRecord>& GetFaultTaskImpacts() const;
    /** Observe already applied task transitions; the observer must not mutate runtime state. */
    void ConnectTaskObserver(Callback<void, const TaskEventRecord&> callback);
    /** Remove a previously connected read-only observer. */
    void DisconnectTaskObserver(Callback<void, const TaskEventRecord&> callback);
    std::map<uint32_t, TaskFaultImpact> ApplyComputeFaultBatch(
        const std::vector<uint32_t>& recoveredNodeIds,
        const std::vector<uint32_t>& startedNodeIds);
    std::map<uint32_t, TaskFaultImpact> ApplyFaultBatch(
        const std::vector<TaskFaultNodeChange>& recoveredNodes,
        const std::vector<TaskFaultNodeChange>& startedNodes);
    bool IsComputeAvailable(uint32_t nodeId) const;
    bool IsSatelliteAvailable(uint32_t nodeId) const;
    /** Fixed-protection fault arbitration only; empty preserves the legacy lifecycle. */
    void SetRecoveryHandler(
        std::function<bool(const TaskRuntime&, const TaskFaultNodeChange&)> handler);
    /** Cancel the primary attempt without failing its logical task or resetting its deadline. */
    bool BeginRecovery(uint64_t taskId);
    /** Generation-guarded recovery transitions. */
    bool RecoveryStarted(uint64_t taskId, uint64_t generation, uint32_t node);
    bool RecoveryComputed(uint64_t taskId, uint64_t generation, uint64_t serviceNs);
    bool RecoveryResult(uint64_t taskId, uint64_t generation, uint64_t transferId, bool local);
    bool FailRecovery(uint64_t taskId,
                      const std::string& cause,
                      TaskFailureReason reason = TaskFailureReason::COMPUTE_NODE_FAILURE);

  private:
    friend struct TaskCoordinatorRecoveryTestAccess; ///< Stale-callback fixture access, not runtime
                                                     ///< API.
    uint32_t GetTaskIndex(uint64_t taskId) const;
    TaskRuntime& GetTask(uint64_t taskId);
    const TaskRuntime& GetTask(uint64_t taskId) const;
    Ptr<ComputeService> FindComputeService(uint32_t nodeId) const;
    Ptr<ComputeService> GetComputeService(uint32_t nodeId) const;
    void TransitionTask(uint64_t taskId,
                        TaskState requestedState,
                        uint32_t nodeId,
                        int64_t eventTimeNs,
                        const std::string& cause);
    TaskFaultImpact FailTaskForComputeNode(
        TaskRuntime& task,
        int64_t eventTimeNs,
        const std::string& cause,
        TaskFailureReason reason = TaskFailureReason::COMPUTE_NODE_FAILURE);
    TaskFaultImpact FailTaskForSatelliteNode(TaskRuntime& task,
                                             uint32_t failedNodeId,
                                             int64_t eventTimeNs,
                                             const std::string& cause);
    void HandleTaskArrival(uint64_t taskId);
    void HandleInputTransferComplete(uint64_t transferId, int64_t completionTimeNs);
    void HandleComputeStart(uint64_t taskId, uint32_t nodeId, int64_t startTimeNs);
    void HandleComputeComplete(uint64_t taskId, uint32_t nodeId, int64_t completionTimeNs);
    void HandleResultTransferComplete(uint64_t transferId, int64_t completionTimeNs);
    void HandleComputeDeadline(uint64_t taskId);
    void CancelComputeDeadline(uint64_t taskId);
    /** Capture an impact before cancellation; never reads a future task execution. */
    void RecordFaultTaskImpact(const TaskRuntime& task,
                              uint32_t faultNodeId,
                              const std::string& impactType);
    void DoDispose() override;

    bool m_initialized{};
    std::vector<TaskRuntime> m_tasks;
    std::map<uint64_t, uint32_t> m_taskIndexes;
    std::vector<Ptr<ComputeService>> m_computeServices;
    std::map<uint32_t, Ptr<ComputeService>> m_servicesByNodeId;
    std::map<uint64_t, uint64_t> m_inputTransferTasks;
    std::map<uint64_t, uint64_t> m_resultTransferTasks;
    std::set<uint32_t> m_unavailableSatelliteNodes;
    std::vector<TaskEventRecord> m_taskEvents;
    TracedCallback<const TaskEventRecord&> m_taskTransition; ///< Optional causal observers.
    std::map<uint32_t, FaultDefinition> m_activeFaults; ///< Only already observed STARTs.
    std::vector<FaultTaskImpactRecord> m_faultTaskImpacts; ///< Sparse impact ledger.
    std::map<uint64_t, EventId> m_deadlineEvents;
    Ptr<NetworkTransferEngine> m_transferEngine;
    std::function<bool(const TaskRuntime&, const TaskFaultNodeChange&)> m_recoveryHandler;
};

} // namespace ns3

#endif
