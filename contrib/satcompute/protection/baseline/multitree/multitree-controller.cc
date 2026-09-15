/* SPDX-License-Identifier: GPL-2.0-only */
#include "multitree-controller.h"
#include "../../../metrics/core/protection-metrics.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
namespace ns3::protection::multitree
{
MultiTreeController::MultiTreeController(Ptr<TaskCoordinator> tasks,
    SatelliteRuntimeView& topology, Ptr<FaultModelEngine> faults, int64_t stopNs,
    std::unique_ptr<PlacementPolicy> placement)
    : m_tasks(tasks), m_decisions(tasks, faults),
      m_recompute(tasks, topology, stopNs, std::move(placement), false),
      m_replicaRequest(m_recompute.Placement()),
      m_replication(tasks, topology, stopNs, m_replicaRequest, m_recompute.Placement(),
                    &m_recompute.Manager(), &m_recompute.PlacementLoads(), false)
{
    auto hooks = m_replication.Hooks();
    hooks.nonParallelFault = [this](const TaskRuntime& task, const TaskFaultNodeChange& change) {
        return m_decisions.Get(task.definition.taskId) == Decision::RS &&
               m_recompute.Recovery().HandleFault(task, change);
    };
    m_tasks->SetParallelAttemptHooks(std::move(hooks));
    m_tasks->ConnectTaskObserver(MakeCallback(&MultiTreeController::OnTask, this));
}
MultiTreeController::~MultiTreeController()
{
    m_tasks->DisconnectTaskObserver(MakeCallback(&MultiTreeController::OnTask, this));
    m_tasks->SetParallelAttemptHooks({});
}
void MultiTreeController::OnTask(const TaskEventRecord& event)
{
    if (event.toState != TASK_RUNNING) return;
    if (m_decisions.Capture(event.taskId).decision == Decision::RP)
        m_replication.Request(event.taskId);
}
void MultiTreeController::Finalize()
{
    if (m_finalized) return;
    // RP attempts settle first. Only the outer finalizer closes remaining logical
    // tasks and the ONE shared transport/load ledger after RS cleanup.
    m_replication.Finalize(false);
    m_recompute.Finalize();
    if (!m_recompute.Manager().IsQuiescent()) throw std::logic_error("Multi-tree ownership leak");
    m_finalized = true;
}
void MultiTreeController::WriteMetrics(const std::filesystem::path& directory) const
{
    if (!m_finalized) throw std::logic_error("Finalize Multi-tree before writing metrics");
    WriteProtectionMetrics(m_recompute.Manager(), *m_tasks->GetTransferEngine(), directory);
    m_recompute.Recovery().WriteMetrics(directory);
    m_replication.WriteMetrics(directory);
    m_recompute.Placement().WriteSelections(directory);
    m_recompute.PlacementLoads().WriteMetrics(directory);
    m_decisions.Write(directory);
    size_t rs = 0, rp = 0, undecided = 0, completed = 0, admitted = 0, takeovers = 0;
    uint64_t recoveryWu = 0, replicaWu = 0;
    uint64_t checkpointPeak = 0;
    for (const auto& [node, pool] : m_recompute.Manager().Pools())
        checkpointPeak = std::max(checkpointPeak, pool->PeakTotal());
    if (checkpointPeak) throw std::logic_error("Multi-tree acquired checkpoint storage");
    for (const auto& task : m_tasks->GetTaskRuntimes())
    {
        const auto decision = m_decisions.Get(task.definition.taskId);
        rs += decision == Decision::RS; rp += decision == Decision::RP;
        undecided += decision == Decision::UNDECIDED;
        completed += task.TaskSucceeded();
    }
    for (const auto& row : m_recompute.Recovery().Summaries()) recoveryWu += row.actualTotalRecoveryWu;
    for (const auto& row : m_replication.Summaries())
    {
        admitted += row.admitted;
        takeovers += row.attempts[1].takeoverNs >= 0;
        replicaWu += row.attempts[1].actualWork;
    }
    nlohmann::json summary = {{"baseline", "Multi-tree (Published FT Rule)"},
        {"task_count", rs + rp + undecided}, {"RS", rs}, {"RP", rp}, {"UNDECIDED", undecided},
        {"completed", completed}, {"failed", rs + rp + undecided - completed},
        {"RP_admitted", admitted}, {"RP_rejected", rp - admitted}, {"RP_takeovers", takeovers},
        {"RS_recovery_attempts", m_recompute.Recovery().Summaries().size()},
        {"RS_actual_recovery_work_units", recoveryWu}, {"RP_actual_replica_work_units", replicaWu},
        {"checkpoint_storage_peak_bytes", checkpointPeak}, {"quiescent", m_recompute.Manager().IsQuiescent()}};
    std::ofstream stream(directory / "multitree-summary.json");
    stream << summary.dump(2) << '\n';
    if (!stream) throw std::runtime_error("Multi-tree summary write failed");
}
} // namespace ns3::protection::multitree
