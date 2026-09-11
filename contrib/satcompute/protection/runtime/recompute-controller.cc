/* SPDX-License-Identifier: GPL-2.0-only */
#include "recompute-controller.h"
#include <stdexcept>

namespace ns3::protection
{
namespace
{
std::unique_ptr<PlacementPolicy>
RequirePlacement(std::unique_ptr<PlacementPolicy> placement)
{
    if (!placement)
        throw std::invalid_argument("Recompute requires an explicit placement policy");
    return placement;
}
} // namespace

RecomputeController::RecomputeController(Ptr<TaskCoordinator> tasks,
                                         SatelliteRuntimeView& topology,
                                         int64_t stopNs,
                                         std::unique_ptr<PlacementPolicy> placement)
    : m_tasks(tasks), m_placement(RequirePlacement(std::move(placement))),
      m_manager(tasks, topology, 0, stopNs),
      m_recovery(tasks, topology, m_manager, stopNs, m_policy,
                 RemoteBusyRecoveryPolicy::RELOCATE, m_placement.get())
{
}

void
RecomputeController::Finalize()
{
    m_tasks->FinalizeSimulation();
    m_recovery.Finalize();
    m_manager.Finalize();
}
} // namespace ns3::protection
