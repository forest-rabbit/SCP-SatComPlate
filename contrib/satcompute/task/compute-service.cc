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

// 以精确整数时长执行单服务槽、非抢占的确定性 FCFS 计算队列。

#include "compute-service.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(ComputeService);

TypeId
ComputeService::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::ComputeService")
      .SetParent<Application>()
      .SetGroupName("SatCompute")
      .AddConstructor<ComputeService>();
  return typeId;
}

ComputeService::ComputeService()
  : m_nodeId(0),
    m_computeRateWorkUnitsPerSecond(0),
    m_configured(false),
    m_isRunning(false),
    m_hasCurrentTask(false),
    m_currentTask({0, 0, -1}),
    m_currentTaskStartTimeNs(-1),
    m_currentTaskServiceTimeNs(0),
    m_enqueuedTaskCount(0),
    m_completedTaskCount(0),
    m_busyTimeNs(0),
    m_maxQueueLength(0)
{
}

ComputeService::~ComputeService()
{
}

bool
ComputeService::WorkItemLess::operator()(const WorkItem& left,
                                         const WorkItem& right) const
{
  return std::make_tuple(left.queueEnterTimeNs, left.taskId)
         < std::make_tuple(right.queueEnterTimeNs, right.taskId);
}

void
ComputeService::Configure(uint32_t nodeId,
                          uint64_t computeRateWorkUnitsPerSecond,
                          TaskEventCallback taskStartedCallback,
                          TaskEventCallback taskCompletedCallback)
{
  NS_ABORT_MSG_IF(m_configured || m_isRunning
                    || !m_queue.empty() || m_hasCurrentTask,
                  "ComputeService 只能在运行前配置一次");
  NS_ABORT_MSG_IF(computeRateWorkUnitsPerSecond == 0,
                  "ComputeService rate 必须为正，node_id=" << nodeId);
  NS_ABORT_MSG_IF(taskStartedCallback.IsNull()
                    || taskCompletedCallback.IsNull(),
                  "ComputeService 生命周期 callback 不能为空，node_id="
                    << nodeId);
  m_nodeId = nodeId;
  m_computeRateWorkUnitsPerSecond =
    computeRateWorkUnitsPerSecond;
  m_taskStartedCallback = taskStartedCallback;
  m_taskCompletedCallback = taskCompletedCallback;
  m_configured = true;
}

int64_t
ComputeService::CalculateServiceTimeNs(
  uint64_t computeWorkUnits,
  uint64_t computeRateWorkUnitsPerSecond)
{
  NS_ABORT_MSG_IF(computeWorkUnits == 0,
                  "compute_work_units 必须为正");
  NS_ABORT_MSG_IF(computeRateWorkUnitsPerSecond == 0,
                  "compute rate 必须为正");

  const unsigned __int128 nanosecondsPerSecond = 1000000000u;
  unsigned __int128 numerator =
    static_cast<unsigned __int128>(computeWorkUnits)
    * nanosecondsPerSecond;
  unsigned __int128 duration =
    (numerator + computeRateWorkUnitsPerSecond - 1)
    / computeRateWorkUnitsPerSecond;
  NS_ABORT_MSG_IF(
    duration
      > static_cast<unsigned __int128>(
          std::numeric_limits<int64_t>::max()),
    "compute duration 超出 ns-3 Time 可表示范围");
  NS_ABORT_MSG_IF(duration == 0,
                  "compute duration 必须至少为 1 ns");
  return static_cast<int64_t>(duration);
}

void
ComputeService::SubmitTask(uint64_t taskId,
                           uint64_t computeWorkUnits,
                           int64_t queueEnterTimeNs)
{
  NS_ABORT_MSG_IF(!m_configured,
                  "ComputeService 尚未配置");
  NS_ABORT_MSG_IF(taskId == 0,
                  "ComputeService 要求正 task_id");
  NS_ABORT_MSG_IF(computeWorkUnits == 0,
                  "ComputeService 要求正 compute_work_units，task_id="
                    << taskId);
  NS_ABORT_MSG_IF(queueEnterTimeNs < 0
                    || queueEnterTimeNs
                         != Simulator::Now().GetNanoSeconds(),
                  "queue_enter_time_ns 必须等于当前仿真时间，task_id="
                    << taskId);
  NS_ABORT_MSG_IF(!m_knownTaskIds.insert(taskId).second,
                  "ComputeService 重复提交 task_id=" << taskId
                    << " node_id=" << m_nodeId);

  WorkItem item = {taskId, computeWorkUnits, queueEnterTimeNs};
  NS_ABORT_MSG_IF(!m_queue.insert(item).second,
                  "ComputeService FCFS key 重复，task_id=" << taskId);
  NS_ABORT_MSG_IF(
    m_enqueuedTaskCount == std::numeric_limits<uint64_t>::max(),
    "ComputeService enqueued task count 溢出，node_id=" << m_nodeId);
  ++m_enqueuedTaskCount;
  m_maxQueueLength =
    std::max(m_maxQueueLength,
             static_cast<uint32_t>(m_queue.size()));
  RequestDispatch();
}

void
ComputeService::StartApplication()
{
  NS_ABORT_MSG_IF(!m_configured,
                  "ComputeService 尚未配置");
  NS_ABORT_MSG_IF(m_isRunning,
                  "ComputeService application 重复启动，node_id=" << m_nodeId);
  m_isRunning = true;
  RequestDispatch();
}

void
ComputeService::StopApplication()
{
  m_isRunning = false;
  if (m_dispatchEvent.IsRunning())
    {
      Simulator::Cancel(m_dispatchEvent);
    }
  if (m_completionEvent.IsRunning())
    {
      Simulator::Cancel(m_completionEvent);
    }
}

void
ComputeService::RequestDispatch()
{
  if (!m_isRunning || m_hasCurrentTask || m_queue.empty()
      || m_dispatchEvent.IsRunning())
    {
      return;
    }
  m_dispatchEvent =
    Simulator::ScheduleNow(&ComputeService::DispatchNextTask, this);
}

void
ComputeService::DispatchNextTask()
{
  if (!m_isRunning || m_hasCurrentTask || m_queue.empty())
    {
      return;
    }

  auto next = m_queue.begin();
  m_currentTask = *next;
  m_queue.erase(next);
  m_hasCurrentTask = true;
  m_currentTaskStartTimeNs = Simulator::Now().GetNanoSeconds();
  m_currentTaskServiceTimeNs =
    CalculateServiceTimeNs(m_currentTask.computeWorkUnits,
                           m_computeRateWorkUnitsPerSecond);
  m_taskStartedCallback(m_currentTask.taskId,
                        m_nodeId,
                        m_currentTaskStartTimeNs);
  m_completionEvent =
    Simulator::Schedule(NanoSeconds(m_currentTaskServiceTimeNs),
                        &ComputeService::CompleteCurrentTask,
                        this);
}

void
ComputeService::CompleteCurrentTask()
{
  NS_ABORT_MSG_IF(!m_isRunning || !m_hasCurrentTask,
                  "ComputeService completion 没有运行任务，node_id="
                    << m_nodeId);
  int64_t completionTimeNs = Simulator::Now().GetNanoSeconds();
  NS_ABORT_MSG_IF(
    completionTimeNs - m_currentTaskStartTimeNs
      != m_currentTaskServiceTimeNs,
    "ComputeService completion time 不符合精确整数时长，task_id="
      << m_currentTask.taskId);
  NS_ABORT_MSG_IF(
    m_busyTimeNs
      > std::numeric_limits<uint64_t>::max()
          - static_cast<uint64_t>(m_currentTaskServiceTimeNs),
    "ComputeService busy time 溢出，node_id=" << m_nodeId);
  m_busyTimeNs += static_cast<uint64_t>(m_currentTaskServiceTimeNs);
  NS_ABORT_MSG_IF(
    m_completedTaskCount == std::numeric_limits<uint64_t>::max(),
    "ComputeService completed task count 溢出，node_id=" << m_nodeId);
  ++m_completedTaskCount;

  uint64_t completedTaskId = m_currentTask.taskId;
  m_hasCurrentTask = false;
  m_currentTask = {0, 0, -1};
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
  return m_busyTimeNs;
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
ComputeService::HasRunningTask() const
{
  return m_hasCurrentTask;
}

uint64_t
ComputeService::GetRunningTaskId() const
{
  NS_ABORT_MSG_IF(!m_hasCurrentTask,
                  "ComputeService 当前没有运行任务，node_id=" << m_nodeId);
  return m_currentTask.taskId;
}

bool
ComputeService::IsIdle() const
{
  return !m_hasCurrentTask && m_queue.empty();
}

void
ComputeService::DoDispose()
{
  m_taskStartedCallback = TaskEventCallback();
  m_taskCompletedCallback = TaskEventCallback();
  Application::DoDispose();
}

} // namespace ns3
