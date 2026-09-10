/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_SERVICE_H
#define SATCOMPUTE_COMPUTE_SERVICE_H

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace ns3
{

/** Causal progress snapshot for the task currently using one compute node. */
struct RunningComputeTaskSnapshot
{
    uint64_t taskId{}; ///< Stable task ID.
    int64_t startTimeNs{}; ///< Exact compute-dispatch time.
    int64_t serviceTimeNs{}; ///< Fixed non-preemptive service duration.
    int64_t elapsedTimeNs{}; ///< Known elapsed service time at this instant.
    int64_t remainingTimeNs{}; ///< Known time until the scheduled completion.
    double completionRatio{}; ///< elapsed/service in [0, 1].
};

/** Real recovery service ledger, retained after completion/cancellation. */
struct RecoveryComputeAccounting
{
    uint64_t plannedWork{}, executedWork{}, rate{}; ///< Integer WU and WU/s.
    int64_t serviceNs{}; ///< Actual occupied compute time, excluding reserved-idle.
};

/** Deterministic, non-preemptive, single-server FCFS compute queue. */
class ComputeService : public Application
{
  public:
    using TaskEventCallback = Callback<void, uint64_t, uint32_t, int64_t>;

    static TypeId GetTypeId();

    ComputeService();
    ~ComputeService() override;

    void Configure(uint32_t nodeId,
                   uint64_t computeRateWorkUnitsPerSecond,
                   TaskEventCallback taskStartedCallback,
                   TaskEventCallback taskCompletedCallback);
    bool SubmitTask(uint64_t taskId,
                    uint64_t computeWorkUnits,
                    int64_t queueEnterTimeNs);
    bool SetComputeAvailable(bool available);
    /** Subscribe to exact busy/idle transitions; callbacks must not change service state. */
    void ConnectStateObserver(Callback<void, uint32_t, bool> callback);
    /** Disconnect a previously registered observer. */
    void DisconnectStateObserver(Callback<void, uint32_t, bool> callback);
    bool CancelRunningTaskForFailure(uint64_t taskId);
    /** Resolve inclusive completion before a same-time deadline, independent of UID. */
    bool CompleteTaskIfDue(uint64_t taskId);
    bool RemoveQueuedTaskForFailure(uint64_t taskId);

    static int64_t CalculateServiceTimeNs(uint64_t computeWorkUnits,
                                          uint64_t computeRateWorkUnitsPerSecond);

    uint32_t GetNodeId() const;
    uint64_t GetComputeRateWorkUnitsPerSecond() const;
    uint64_t GetEnqueuedTaskCount() const;
    uint64_t GetCompletedTaskCount() const;
    uint64_t GetBusyTimeNs() const;
    uint32_t GetMaxQueueLength() const;
    uint32_t GetQueueSize() const;
    bool IsComputeAvailable() const;
    bool HasRunningTask() const;
    uint64_t GetRunningTaskId() const;
    /** @return Current task progress, or null when the service is not running a task. */
    std::optional<RunningComputeTaskSnapshot> GetRunningTaskSnapshot() const;
    bool IsIdle() const;
    /** Reserve an idle service for one recovery attempt; ordinary arrivals remain queued. */
    bool ReserveRecovery(uint64_t taskId, uint64_t generation);
    /** Release a matching non-running reservation; never affects another attempt. */
    bool ReleaseRecovery(uint64_t taskId, uint64_t generation);
    /** True while a recovery owns the slot, including reserved-idle wait. */
    bool HasRecoveryReservation() const;
    /** Execute real remaining work in this service, retaining F1/F2 attempt immunity.
     * All callbacks carry task, generation, node and observed ns. Catchup is an actual
     * service milestone, not a second compute simulation or full task completion.
     */
    bool StartRecovery(uint64_t taskId,
                       uint64_t generation,
                       uint64_t workUnits,
                       uint64_t catchupWorkUnits,
                       Callback<void, uint64_t, uint64_t, uint32_t, int64_t> started,
                       Callback<void, uint64_t, uint64_t, uint32_t, int64_t> catchup,
                       Callback<void, uint64_t, uint64_t, uint32_t, int64_t> completed);
    /** Cancel only the matching recovery; F3/deadline cleanup, not F1/F2 availability. */
    bool CancelRecovery(uint64_t taskId, uint64_t generation);
    /** Read observed work, never planned work substituted for an interrupted attempt. */
    RecoveryComputeAccounting GetRecoveryAccounting(uint64_t taskId, uint64_t generation) const;
    uint64_t GetCancelledRunningTaskCount() const;
    uint64_t GetRemovedQueuedTaskCount() const;

  private:
    struct WorkItem
    {
        uint64_t taskId{};
        uint64_t computeWorkUnits{};
        int64_t queueEnterTimeNs{-1};
    };

    struct WorkItemLess
    {
        bool operator()(const WorkItem& left, const WorkItem& right) const;
    };

    void StartApplication() override;
    void StopApplication() override;
    void DoDispose() override;
    void RequestDispatch();
    void DispatchNextTask();
    void CompleteCurrentTask();
    /** Notify observers after a service-state transition, before dependent dispatch. */
    void NotifyComputeState();
    void RecoveryCatchup(uint64_t taskId, uint64_t generation); ///< Guarded service milestone.
    void RecordRecoveryAccounting(); ///< Freeze observed prefix before releasing service state.

    uint32_t m_nodeId{};
    uint64_t m_computeRateWorkUnitsPerSecond{};
    bool m_configured{};
    bool m_isRunning{};
    bool m_computeAvailable{true};
    bool m_hasCurrentTask{};
    WorkItem m_currentTask;
    int64_t m_currentTaskStartTimeNs{-1};
    int64_t m_currentTaskServiceTimeNs{};
    std::set<WorkItem, WorkItemLess> m_queue;
    std::set<uint64_t> m_knownTaskIds;
    EventId m_dispatchEvent;
    EventId m_completionEvent;
    std::optional<std::pair<uint64_t, uint64_t>> m_recoveryOwner; ///< Reserved task/generation.
    bool m_runningRecovery{}; ///< Only this attempt ignores compute availability.
    std::map<std::pair<uint64_t, uint64_t>, RecoveryComputeAccounting> m_recoveryAccounting;
    EventId m_catchupEvent;   ///< Cancelled with the owning recovery.
    Callback<void, uint64_t, uint64_t, uint32_t, int64_t> m_recoveryCatchup;
    Callback<void, uint64_t, uint64_t, uint32_t, int64_t> m_recoveryCompleted;
    TaskEventCallback m_taskStartedCallback;
    TaskEventCallback m_taskCompletedCallback;
    TracedCallback<uint32_t, bool> m_computeState; ///< Node ID and effective busy state.
    uint64_t m_enqueuedTaskCount{};
    uint64_t m_completedTaskCount{};
    uint64_t m_cancelledRunningTaskCount{};
    uint64_t m_removedQueuedTaskCount{};
    uint64_t m_busyTimeNs{};
    uint32_t m_maxQueueLength{};
};

} // namespace ns3

#endif
