/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_ONE_PLUS_ONE_CONTROLLER_H
#define SATCOMPUTE_ONE_PLUS_ONE_CONTROLLER_H
#include "../mechanism/replication/replica-manager.h"

namespace ns3::protection
{
/** First-TASK_RUNNING wiring; policy admission never delays primary execution. */
class OnePlusOneController
{
  public:
    OnePlusOneController(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                          int64_t stopNs, std::unique_ptr<PlacementPolicy> placement);
    ~OnePlusOneController();
    void Finalize() { m_manager.Finalize(); }
    const ReplicaManager& Manager() const { return m_manager; }

  private:
    void OnTask(const TaskEventRecord& event);
    Ptr<TaskCoordinator> m_tasks;
    std::unique_ptr<PlacementPolicy> m_placement;
    OnePlusOnePolicy m_policy;
    ReplicaManager m_manager;
};
} // namespace ns3::protection
#endif
