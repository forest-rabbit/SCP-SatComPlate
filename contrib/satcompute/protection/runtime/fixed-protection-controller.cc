/* SPDX-License-Identifier: GPL-2.0-only */
#include "fixed-protection-controller.h"

namespace ns3::protection
{
FixedProtectionController::FixedProtectionController(Ptr<TaskCoordinator> tasks,
                                                     SatelliteRuntimeView& topology,
                                                     uint64_t capacity,
                                                     int64_t stopNs,
                                                     uint32_t deltaPermille,
                                                     uint32_t batchN,
                                                     bool enableRecovery)
    : m_tasks(tasks), m_topology(topology), m_manager(tasks, topology, capacity, stopNs),
      m_policy(deltaPermille, batchN), m_runtime(m_policy, {&m_manager})
{
    m_tasks->ConnectTaskObserver(MakeCallback(&FixedProtectionController::OnTask, this));
    if (enableRecovery)
        m_recovery = std::make_unique<RecoveryController>(tasks, topology, m_manager, stopNs, m_policy);
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
                 oneHop});
        }
        m_runtime.OnTaskComputeStart(context);
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
    m_manager.Finalize();
}
} // namespace ns3::protection
