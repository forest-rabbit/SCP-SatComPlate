/* SPDX-License-Identifier: GPL-2.0-only */
#include "transfer-only-recovery-ledger.h"
#include "../common/task-state-adapter.h"
#include "ns3/simulator.h"
#include <stdexcept>

namespace ns3::protection
{
TransferOnlyRecoveryLedger::TransferOnlyRecoveryLedger(
    Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology, int64_t stopNs)
    : m_tasks(tasks), m_transfers(tasks)
{
    if (stopNs <= 0) throw std::invalid_argument("invalid checkpoint duration");
    for (const auto& service : tasks->GetComputeServices())
    {
        if (!topology.HasSatelliteId(service->GetNodeId()))
            throw std::logic_error("unknown compute satellite");
        m_pools.emplace(service->GetNodeId(), std::make_unique<BackupStoragePool>(0));
    }
    // Preserve the existing early budget validation even though no checkpoint is created.
    for (const auto& task : tasks->GetTaskRuntimes()) TaskStateAdapter{task.definition};
}
void TransferOnlyRecoveryLedger::Finalize() { m_transfers.Finalize(); }
RecoverySnapshot TransferOnlyRecoveryLedger::FreezeRecoverySnapshot(uint64_t id, int64_t at)
{
    return FreezePrimaryRecoverySnapshot(m_tasks, id, at);
}
void TransferOnlyRecoveryLedger::ReleaseRecoveryState(uint64_t task)
{
    for (auto& [node, pool] : m_pools) pool->ReleaseTask(task);
}
void TransferOnlyRecoveryLedger::RecordRecoveryEvent(const RecoverySnapshot& snapshot,
    const std::string& event, uint64_t bytes, uint64_t transfer)
{
    m_events.push_back(MakeRecoveryEvidence(snapshot, event, bytes, transfer,
                                           Simulator::Now().GetNanoSeconds(), m_pools));
}
bool TransferOnlyRecoveryLedger::IsQuiescent() const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->HasRecoveryReservation()) return false;
    const auto network = m_tasks->GetTransferEngine();
    for (const auto& row : network->CollectSummaries())
        if (network->IsRuntimeTransfer(row.transferId) && row.transferState != "COMPLETED" &&
            row.transferState != "FAILED" && row.transferState != "CANCELLED") return false;
    if (!m_transfers.Empty()) return false;
    for (const auto& [node, pool] : m_pools)
        if (pool->Used() || pool->Reserved()) return false;
    return true;
}
} // namespace ns3::protection
