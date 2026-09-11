/* SPDX-License-Identifier: GPL-2.0-only */
#include "recompute-controller.h"
#include <stdexcept>
#include "ns3/simulator.h"

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
    for (auto service : tasks->GetComputeServices()) m_loads.RegisterNode(service->GetNodeId());
    m_recovery.SetPlacementLoads(&m_loads);
    m_recovery.SetLoadObserver([this](auto task, auto node, bool active) {
        m_loads.Recovery(task, node, active, Simulator::Now().GetNanoSeconds());
    });
}

void
RecomputeController::Finalize()
{
    m_tasks->FinalizeSimulation();
    m_recovery.Finalize();
    m_manager.Finalize();
    if (!m_loads.Empty()) throw std::logic_error("recompute placement load leaked");
}
} // namespace ns3::protection
