/* SPDX-License-Identifier: GPL-2.0-only */
#include "one-plus-one-controller.h"
#include <stdexcept>

namespace ns3::protection
{
namespace
{
std::unique_ptr<PlacementPolicy> RequirePlacement(std::unique_ptr<PlacementPolicy> policy)
{
    if (!policy) throw std::invalid_argument("1+1 requires an explicit placement policy");
    return policy;
}
} // namespace

OnePlusOneController::OnePlusOneController(Ptr<TaskCoordinator> tasks,
    SatelliteRuntimeView& topology, int64_t stopNs, std::unique_ptr<PlacementPolicy> placement)
    : m_tasks(tasks), m_placement(RequirePlacement(std::move(placement))),
      m_policy(*m_placement), m_manager(tasks, topology, stopNs, m_policy)
{
    m_tasks->ConnectTaskObserver(MakeCallback(&OnePlusOneController::OnTask, this));
}

OnePlusOneController::~OnePlusOneController()
{
    m_tasks->DisconnectTaskObserver(MakeCallback(&OnePlusOneController::OnTask, this));
}

void OnePlusOneController::OnTask(const TaskEventRecord& event)
{
    if (event.toState == TASK_RUNNING) m_manager.Request(event.taskId);
}
} // namespace ns3::protection
