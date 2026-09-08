/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Execute compute work using exact integer-nanosecond service durations.

#include "compute-service.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(ComputeService);

TypeId
ComputeService::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::ComputeService")
                               .SetParent<Application>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<ComputeService>();
    return typeId;
}

ComputeService::ComputeService() = default;

ComputeService::~ComputeService() = default;

bool
ComputeService::WorkItemLess::operator()(const WorkItem& left, const WorkItem& right) const
{
    return std::make_tuple(left.queueEnterTimeNs, left.taskId) <
           std::make_tuple(right.queueEnterTimeNs, right.taskId);
}

void
ComputeService::Configure(uint32_t nodeId,
                          uint64_t computeRateWorkUnitsPerSecond,
                          TaskEventCallback taskStartedCallback,
                          TaskEventCallback taskCompletedCallback)
{
    NS_ABORT_MSG_IF(m_configured || m_isRunning || !m_queue.empty() || m_hasCurrentTask,
                    "ComputeService can only be configured once before it runs");
    NS_ABORT_MSG_IF(computeRateWorkUnitsPerSecond == 0,
                    "ComputeService rate must be positive");
    NS_ABORT_MSG_IF(taskStartedCallback.IsNull() || taskCompletedCallback.IsNull(),
                    "ComputeService lifecycle callbacks must not be null");
    m_nodeId = nodeId;
    m_computeRateWorkUnitsPerSecond = computeRateWorkUnitsPerSecond;
    m_taskStartedCallback = taskStartedCallback;
    m_taskCompletedCallback = taskCompletedCallback;
    m_configured = true;
}

int64_t
ComputeService::CalculateServiceTimeNs(uint64_t computeWorkUnits,
                                       uint64_t computeRateWorkUnitsPerSecond)
{
    NS_ABORT_MSG_IF(computeWorkUnits == 0, "compute work must be positive");
    NS_ABORT_MSG_IF(computeRateWorkUnitsPerSecond == 0, "compute rate must be positive");

    constexpr unsigned __int128 nanosecondsPerSecond = 1000000000u;
    const unsigned __int128 numerator =
        static_cast<unsigned __int128>(computeWorkUnits) * nanosecondsPerSecond;
    const unsigned __int128 duration =
        (numerator + computeRateWorkUnitsPerSecond - 1) /
        computeRateWorkUnitsPerSecond;
    NS_ABORT_MSG_IF(duration >
                        static_cast<unsigned __int128>(std::numeric_limits<int64_t>::max()),
                    "compute duration exceeds ns-3 Time range");
    NS_ABORT_MSG_IF(duration == 0, "compute duration must be at least one nanosecond");
    return static_cast<int64_t>(duration);
}

bool
ComputeService::SubmitTask(uint64_t taskId,
                           uint64_t computeWorkUnits,
                           int64_t queueEnterTimeNs)
{
    NS_ABORT_MSG_IF(!m_configured, "ComputeService is not configured");
    NS_ABORT_MSG_IF(taskId == 0, "ComputeService requires a positive task ID");
    NS_ABORT_MSG_IF(computeWorkUnits == 0, "ComputeService requires positive compute work");
    NS_ABORT_MSG_IF(queueEnterTimeNs < 0 ||
                        queueEnterTimeNs != Simulator::Now().GetNanoSeconds(),
                    "queue entry time must equal the current simulation time");
    // Temporary compute outages stop dispatch, not admission to the FCFS queue.
    NS_ABORT_MSG_IF(!m_knownTaskIds.insert(taskId).second,
                    "ComputeService received a duplicate task ID");

    const WorkItem item = {taskId, computeWorkUnits, queueEnterTimeNs};
    NS_ABORT_MSG_IF(!m_queue.insert(item).second, "ComputeService FCFS key is not unique");
    NS_ABORT_MSG_IF(m_enqueuedTaskCount == std::numeric_limits<uint64_t>::max(),
                    "ComputeService enqueue counter overflow");
    ++m_enqueuedTaskCount;
    m_maxQueueLength = std::max(m_maxQueueLength, static_cast<uint32_t>(m_queue.size()));
    RequestDispatch();
    return true;
}

bool
ComputeService::SetComputeAvailable(bool available)
{
    NS_ABORT_MSG_IF(!m_configured, "ComputeService is not configured");
    if (m_computeAvailable == available)
    {
        return false;
    }
    m_computeAvailable = available;
    if (!available && m_dispatchEvent.IsPending())
    {
        Simulator::Cancel(m_dispatchEvent);
    }
    if (available)
    {
        RequestDispatch();
    }
    return true;
}

bool
ComputeService::CancelRunningTaskForFailure(uint64_t taskId)
{
    NS_ABORT_MSG_IF(taskId == 0, "ComputeService requires a positive task ID");
    if (!m_hasCurrentTask || m_currentTask.taskId != taskId)
    {
        return false;
    }
    if (m_completionEvent.IsPending())
    {
        Simulator::Cancel(m_completionEvent);
    }
    const auto elapsed =
        static_cast<uint64_t>(Simulator::Now().GetNanoSeconds() - m_currentTaskStartTimeNs);
    NS_ABORT_MSG_IF(elapsed > static_cast<uint64_t>(m_currentTaskServiceTimeNs) ||
                        m_busyTimeNs > std::numeric_limits<uint64_t>::max() - elapsed,
                    "cancelled compute busy-time overflow");
    m_busyTimeNs += elapsed;
    m_hasCurrentTask = false;
    m_currentTask = {};
    m_currentTaskStartTimeNs = -1;
    m_currentTaskServiceTimeNs = 0;
    NS_ABORT_MSG_IF(m_cancelledRunningTaskCount ==
                        std::numeric_limits<uint64_t>::max(),
                    "ComputeService running-task cancellation counter overflow");
    ++m_cancelledRunningTaskCount;
    RequestDispatch();
    return true;
}

bool
ComputeService::CompleteTaskIfDue(uint64_t taskId)
{
    if (!m_isRunning || !m_computeAvailable || !m_hasCurrentTask ||
        m_currentTask.taskId != taskId ||
        Simulator::Now().GetNanoSeconds() - m_currentTaskStartTimeNs != m_currentTaskServiceTimeNs)
    {
        return false;
    }
    if (m_completionEvent.IsPending())
        Simulator::Cancel(m_completionEvent);
    CompleteCurrentTask();
    return true;
}

bool
ComputeService::RemoveQueuedTaskForFailure(uint64_t taskId)
{
    NS_ABORT_MSG_IF(taskId == 0, "ComputeService requires a positive task ID");
    const auto queued = std::find_if(m_queue.begin(),
                                     m_queue.end(),
                                     [taskId](const WorkItem& item) {
                                         return item.taskId == taskId;
                                     });
    if (queued == m_queue.end())
    {
        return false;
    }
    m_queue.erase(queued);
    NS_ABORT_MSG_IF(m_removedQueuedTaskCount ==
                        std::numeric_limits<uint64_t>::max(),
                    "ComputeService queued-task removal counter overflow");
    ++m_removedQueuedTaskCount;
    return true;
}

void
ComputeService::StartApplication()
{
    NS_ABORT_MSG_IF(!m_configured, "ComputeService is not configured");
    NS_ABORT_MSG_IF(m_isRunning, "ComputeService application started twice");
    m_isRunning = true;
    RequestDispatch();
}

void
ComputeService::StopApplication()
{
    if (m_isRunning && m_hasCurrentTask)
    {
        m_busyTimeNs = GetBusyTimeNs();
    }
    m_isRunning = false;
    if (m_dispatchEvent.IsPending())
    {
        Simulator::Cancel(m_dispatchEvent);
    }
    if (m_completionEvent.IsPending())
    {
        Simulator::Cancel(m_completionEvent);
    }
}

void
ComputeService::RequestDispatch()
{
    if (!m_isRunning || !m_computeAvailable || m_hasCurrentTask || m_queue.empty() ||
        m_dispatchEvent.IsPending())
    {
        return;
    }
    m_dispatchEvent = Simulator::ScheduleNow(&ComputeService::DispatchNextTask, this);
}

void
ComputeService::DispatchNextTask()
{
    if (!m_isRunning || !m_computeAvailable || m_hasCurrentTask || m_queue.empty())
    {
        return;
    }

    const auto next = m_queue.begin();
    m_currentTask = *next;
    m_queue.erase(next);
    m_hasCurrentTask = true;
    m_currentTaskStartTimeNs = Simulator::Now().GetNanoSeconds();
    m_currentTaskServiceTimeNs = CalculateServiceTimeNs(
        m_currentTask.computeWorkUnits,
        m_computeRateWorkUnitsPerSecond);
    m_taskStartedCallback(m_currentTask.taskId, m_nodeId, m_currentTaskStartTimeNs);
    m_completionEvent = Simulator::Schedule(NanoSeconds(m_currentTaskServiceTimeNs),
                                            &ComputeService::CompleteCurrentTask,
                                            this);
}

void
ComputeService::CompleteCurrentTask()
{
    NS_ABORT_MSG_IF(!m_isRunning || !m_computeAvailable || !m_hasCurrentTask,
                    "ComputeService completion has no running task");
    const int64_t completionTimeNs = Simulator::Now().GetNanoSeconds();
    NS_ABORT_MSG_IF(completionTimeNs - m_currentTaskStartTimeNs !=
                        m_currentTaskServiceTimeNs,
                    "ComputeService completion violated exact integer duration");
    NS_ABORT_MSG_IF(m_busyTimeNs >
                        std::numeric_limits<uint64_t>::max() -
                            static_cast<uint64_t>(m_currentTaskServiceTimeNs),
                    "ComputeService busy-time counter overflow");
    m_busyTimeNs += static_cast<uint64_t>(m_currentTaskServiceTimeNs);
    NS_ABORT_MSG_IF(m_completedTaskCount == std::numeric_limits<uint64_t>::max(),
                    "ComputeService completion counter overflow");
    ++m_completedTaskCount;

    const uint64_t completedTaskId = m_currentTask.taskId;
    m_hasCurrentTask = false;
    m_currentTask = {};
    m_currentTaskStartTimeNs = -1;
    m_currentTaskServiceTimeNs = 0;
    m_taskCompletedCallback(completedTaskId, m_nodeId, completionTimeNs);
    DispatchNextTask();
}

uint32_t
ComputeService::GetNodeId() const
{
    return m_nodeId;
}

uint64_t
ComputeService::GetComputeRateWorkUnitsPerSecond() const
{
    return m_computeRateWorkUnitsPerSecond;
}

uint64_t
ComputeService::GetEnqueuedTaskCount() const
{
    return m_enqueuedTaskCount;
}

uint64_t
ComputeService::GetCompletedTaskCount() const
{
    return m_completedTaskCount;
}

uint64_t
ComputeService::GetBusyTimeNs() const
{
    return m_busyTimeNs + (m_isRunning && m_hasCurrentTask
                               ? static_cast<uint64_t>(Simulator::Now().GetNanoSeconds() -
                                                       m_currentTaskStartTimeNs)
                               : 0);
}

uint32_t
ComputeService::GetMaxQueueLength() const
{
    return m_maxQueueLength;
}

uint32_t
ComputeService::GetQueueSize() const
{
    return static_cast<uint32_t>(m_queue.size());
}

bool
ComputeService::IsComputeAvailable() const
{
    return m_computeAvailable;
}

bool
ComputeService::HasRunningTask() const
{
    return m_hasCurrentTask;
}

uint64_t
ComputeService::GetRunningTaskId() const
{
    NS_ABORT_MSG_IF(!m_hasCurrentTask, "ComputeService has no running task");
    return m_currentTask.taskId;
}

std::optional<RunningComputeTaskSnapshot>
ComputeService::GetRunningTaskSnapshot() const
{
    if (!m_hasCurrentTask)
    {
        return std::nullopt;
    }
    const int64_t elapsedTimeNs =
        Simulator::Now().GetNanoSeconds() - m_currentTaskStartTimeNs;
    NS_ABORT_MSG_IF(m_currentTaskStartTimeNs < 0 ||
                        m_currentTaskServiceTimeNs <= 0 || elapsedTimeNs < 0 ||
                        elapsedTimeNs > m_currentTaskServiceTimeNs,
                    "ComputeService running-task timing is invalid");
    return RunningComputeTaskSnapshot{
        m_currentTask.taskId,
        m_currentTaskStartTimeNs,
        m_currentTaskServiceTimeNs,
        elapsedTimeNs,
        m_currentTaskServiceTimeNs - elapsedTimeNs,
        static_cast<double>(elapsedTimeNs) /
            static_cast<double>(m_currentTaskServiceTimeNs)};
}

bool
ComputeService::IsIdle() const
{
    return !m_hasCurrentTask && m_queue.empty();
}

uint64_t
ComputeService::GetCancelledRunningTaskCount() const
{
    return m_cancelledRunningTaskCount;
}

uint64_t
ComputeService::GetRemovedQueuedTaskCount() const
{
    return m_removedQueuedTaskCount;
}

void
ComputeService::DoDispose()
{
    m_taskStartedCallback = {};
    m_taskCompletedCallback = {};
    Application::DoDispose();
}

} // namespace ns3
