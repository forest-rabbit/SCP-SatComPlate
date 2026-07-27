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

#ifndef SATCOMPUTE_COMPUTE_SERVICE_H
#define SATCOMPUTE_COMPUTE_SERVICE_H

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"

#include <cstdint>
#include <set>

namespace ns3 {

class ComputeService : public Application
{
public:
  typedef Callback<void, uint64_t, uint32_t, int64_t> TaskEventCallback;

  static TypeId GetTypeId();

  ComputeService();
  ~ComputeService() override;

  void Configure(uint32_t nodeId,
                 uint64_t computeRateWorkUnitsPerSecond,
                 TaskEventCallback taskStartedCallback,
                 TaskEventCallback taskCompletedCallback);
  void SubmitTask(uint64_t taskId,
                  uint64_t computeWorkUnits,
                  int64_t queueEnterTimeNs);

  static int64_t CalculateServiceTimeNs(
    uint64_t computeWorkUnits,
    uint64_t computeRateWorkUnitsPerSecond);

  uint32_t GetNodeId() const;
  uint64_t GetComputeRateWorkUnitsPerSecond() const;
  uint64_t GetEnqueuedTaskCount() const;
  uint64_t GetCompletedTaskCount() const;
  uint64_t GetBusyTimeNs() const;
  uint32_t GetMaxQueueLength() const;
  uint32_t GetQueueSize() const;
  bool HasRunningTask() const;
  uint64_t GetRunningTaskId() const;
  bool IsIdle() const;

private:
  struct WorkItem
  {
    uint64_t taskId;
    uint64_t computeWorkUnits;
    int64_t queueEnterTimeNs;
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

  uint32_t m_nodeId;
  uint64_t m_computeRateWorkUnitsPerSecond;
  bool m_configured;
  bool m_isRunning;
  bool m_hasCurrentTask;
  WorkItem m_currentTask;
  int64_t m_currentTaskStartTimeNs;
  int64_t m_currentTaskServiceTimeNs;
  std::set<WorkItem, WorkItemLess> m_queue;
  std::set<uint64_t> m_knownTaskIds;
  EventId m_dispatchEvent;
  EventId m_completionEvent;
  TaskEventCallback m_taskStartedCallback;
  TaskEventCallback m_taskCompletedCallback;
  uint64_t m_enqueuedTaskCount;
  uint64_t m_completedTaskCount;
  uint64_t m_busyTimeNs;
  uint32_t m_maxQueueLength;
};

} // namespace ns3

#endif
