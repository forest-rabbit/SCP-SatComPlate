/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_SERVICE_H
#define SATCOMPUTE_COMPUTE_SERVICE_H

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"

#include <cstdint>
#include <set>

namespace ns3
{

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
    bool CancelRunningTaskForFailure(uint64_t taskId);
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
    bool IsIdle() const;
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
    TaskEventCallback m_taskStartedCallback;
    TaskEventCallback m_taskCompletedCallback;
    uint64_t m_enqueuedTaskCount{};
    uint64_t m_completedTaskCount{};
    uint64_t m_cancelledRunningTaskCount{};
    uint64_t m_removedQueuedTaskCount{};
    uint64_t m_busyTimeNs{};
    uint32_t m_maxQueueLength{};
};

} // namespace ns3

#endif
