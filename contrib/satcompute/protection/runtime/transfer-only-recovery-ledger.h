/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_TRANSFER_ONLY_RECOVERY_LEDGER_H
#define SATCOMPUTE_TRANSFER_ONLY_RECOVERY_LEDGER_H
#include "checkpoint-recovery-port.h"
#include "../../topology/satellite-runtime-view.h"

namespace ns3::protection
{
/** Baseline transport/evidence adapter, with no checkpoint generation/execution capability.
 * Zero-capacity node rows retain the existing metrics schema; no checkpoint objects exist.
 */
class TransferOnlyRecoveryLedger final : public CheckpointRecoveryPort
{
  public:
    TransferOnlyRecoveryLedger(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology, int64_t stopNs);
    void Finalize();
    InputStagingPolicy InputPolicy() const override { return InputStagingPolicy::EAGER; }
    uint64_t GlobalStoragePeakBytes() const override { return 0; }
    const std::vector<ProtectionEvent>& Events() const override { return m_events; }
    const std::vector<ProtectionFlow>& Flows() const override { return m_transfers.Flows(); }
    std::vector<ProtectionTaskSummary> Summaries() const override { return {}; }
    const BackupPools& Pools() const override { return m_pools; }
    bool IsQuiescent() const override;
    void EnableRecoveryRetention() override {}
    RecoverySnapshot FreezeRecoverySnapshot(uint64_t id, int64_t at) override;
    void QuiesceForRecovery(const RecoverySnapshot&) override {}
    void ReleaseRecoveryState(uint64_t task) override;
    void RecordRecoveryEvent(const RecoverySnapshot& snapshot, const std::string& event,
                             uint64_t bytes, uint64_t transfer) override;
    ProtectionTransferDispatcher& Transfers() override { return m_transfers; }

  private:
    Ptr<TaskCoordinator> m_tasks;
    ProtectionTransferDispatcher m_transfers;
    BackupPools m_pools; ///< Compatibility zero-capacity rows, not hidden checkpoint storage.
    std::vector<ProtectionEvent> m_events;
};
} // namespace ns3::protection
#endif
