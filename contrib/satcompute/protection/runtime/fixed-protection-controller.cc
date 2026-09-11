/* SPDX-License-Identifier: GPL-2.0-only */
#include "fixed-protection-controller.h"
#include "decision-path-snapshot.h"
#include "ns3/simulator.h"
#include <stdexcept>

namespace ns3::protection
{
FixedProtectionController::FixedProtectionController(Ptr<TaskCoordinator> tasks,
                                                     SatelliteRuntimeView& topology,
                                                     uint64_t capacity,
                                                     int64_t stopNs,
                                                     uint32_t deltaPermille,
                                                     uint32_t batchN,
                                                     bool enableRecovery,
                                                     std::unique_ptr<PlacementPolicy> placement,
                                                     RemoteBusyRecoveryPolicy busyPolicy)
    : m_tasks(tasks), m_topology(topology), m_manager(tasks, topology, capacity, stopNs),
      m_policy(deltaPermille, batchN, std::move(placement)), m_runtime(m_policy, {&m_manager})
{
    for (auto service : tasks->GetComputeServices()) m_loads.RegisterNode(service->GetNodeId());
    m_manager.SetAssignmentObserver([this](auto task, auto node, bool active) {
        m_loads.Assignment(task, node, active, Simulator::Now().GetNanoSeconds());
    });
    m_tasks->ConnectTaskObserver(MakeCallback(&FixedProtectionController::OnTask, this));
    if (enableRecovery)
    {
        m_recovery = std::make_unique<RecoveryController>(tasks, topology, m_manager, stopNs, m_policy, busyPolicy);
        m_recovery->SetLoadObserver([this](auto task, auto node, bool active) {
            m_loads.Recovery(task, node, active, Simulator::Now().GetNanoSeconds());
        });
    }
}

FixedProtectionController::~FixedProtectionController()
{
    m_tasks->DisconnectTaskObserver(MakeCallback(&FixedProtectionController::OnTask, this));
}

void
FixedProtectionController::OnTask(const TaskEventRecord& event)
{
    if (event.toState == TASK_RUNNING)
    {
        ProtectionContext context;
        context.attempt = {event.taskId, 0};
        context.primaryNode = event.nodeId;
        context.nowNs = event.simulationTimeNs;
        context.firstComputeStart = true;
        context.taskSelected = true;
        for (auto service : m_tasks->GetComputeServices())
        {
            const auto node = service->GetNodeId();
            if (node == event.nodeId)
                continue;
            const auto routes = m_topology.GetEcmpRouteCandidates(event.nodeId, node);
            bool oneHop = false;
            for (const auto& route : routes)
                if (m_topology.GetNextHopSatelliteId(event.nodeId, route.outputInterface) == node)
                    oneHop = true;
            context.candidates.push_back(
                {node,
                 m_tasks->IsComputeAvailable(node) && m_tasks->IsSatelliteAvailable(node),
                 service->IsIdle(),
                 !routes.empty(),
                 oneHop,
                 0,
                 m_manager.Pools().at(node)->Free(),
                 m_loads.Get(node).activeBackup,
                 m_loads.Get(node).activeRecovery});
        }
        DecisionPathSnapshot paths([this](auto source, auto destination) {
            return m_tasks->GetTransferEngine()->EstimateAdmissiblePath(source, destination);
        });
        context.previewPath = [&](auto source, auto destination) {
            return paths.Availability(source, destination);
        };
        m_runtime.OnTaskComputeStart(context);
        if (const auto inventory = m_manager.Inventory(event.taskId))
            m_policy.Placement().RecordAdmission(event.taskId, event.simulationTimeNs,
                inventory->active ? "ACCEPTED" : "REJECTED",
                inventory->active ? "INITIALIZATION_PENDING" : "INITIALIZATION_REJECTED");
    }
    if (event.toState == TASK_RESULT_TRANSFERRING && event.fromState != TASK_RUNNING_BACKUP)
        m_runtime.OnTaskComputeComplete({event.taskId, 0});
    if (IsTerminalTaskState(event.toState))
        m_runtime.OnTaskTerminal(event.taskId);
}

void
FixedProtectionController::Finalize()
{
    if (m_recovery)
        m_recovery->Finalize();
    m_tasks->FinalizeSimulation();
    m_manager.Finalize();
    if (!m_loads.Empty()) throw std::logic_error("fixed placement ownership leaked");
}
} // namespace ns3::protection
