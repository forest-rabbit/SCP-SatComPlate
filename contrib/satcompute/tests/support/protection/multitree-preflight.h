/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_MULTITREE_PREFLIGHT_H
#define SATCOMPUTE_MULTITREE_PREFLIGHT_H
#include "ns3/multitree-decision-log.h"
/** Test-driver-only observer; no flow, timer, RNG, reservation or runtime action. */
class MultiTreePreflight
{
  public:
    MultiTreePreflight(ns3::Ptr<ns3::TaskCoordinator> tasks,
                      ns3::Ptr<ns3::FaultModelEngine> faults)
        : m_tasks(tasks), m_log(tasks, faults)
    {
        m_tasks->ConnectTaskObserver(ns3::MakeCallback(&MultiTreePreflight::OnTask, this));
    }
    ~MultiTreePreflight()
    {
        m_tasks->DisconnectTaskObserver(ns3::MakeCallback(&MultiTreePreflight::OnTask, this));
    }
    void Write(const std::filesystem::path& directory) const { m_log.Write(directory); }
  private:
    void OnTask(const ns3::TaskEventRecord& event)
    {
        if (event.toState == ns3::TASK_RUNNING) m_log.Capture(event.taskId);
    }
    ns3::Ptr<ns3::TaskCoordinator> m_tasks;
    ns3::protection::multitree::DecisionLog m_log;
};
#endif
