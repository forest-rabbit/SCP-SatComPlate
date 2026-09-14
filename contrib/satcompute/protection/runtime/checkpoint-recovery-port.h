/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CHECKPOINT_RECOVERY_PORT_H
#define SATCOMPUTE_CHECKPOINT_RECOVERY_PORT_H
#include "protection-evidence-view.h"
namespace ns3::protection
{
/** Primary progress snapshot, before any checkpoint-specific retained object description. */
RecoverySnapshot FreezePrimaryRecoverySnapshot(Ptr<TaskCoordinator> tasks, uint64_t id, int64_t at);
/** Identical evidence row for checkpoint recovery and from-zero recovery; no policy decision. */
ProtectionEvent MakeRecoveryEvidence(const RecoverySnapshot& snapshot, const std::string& event,
                                    uint64_t bytes, uint64_t transfer, int64_t at,
                                    const BackupPools& pools);
/** Optional checkpoint access consumed by shared recovery, never a dependency on Frequency/P.
 * The checkpoint owner implements retention. A transfer-only baseline can expose an OFF snapshot.
 */
class CheckpointRecoveryPort : public ProtectionEvidenceView
{
  public:
    virtual void EnableRecoveryRetention() = 0;
    virtual RecoverySnapshot FreezeRecoverySnapshot(uint64_t id, int64_t at) = 0;
    virtual void QuiesceForRecovery(const RecoverySnapshot& snapshot) = 0;
    virtual void ReleaseRecoveryState(uint64_t task) = 0;
    virtual void RecordRecoveryEvent(const RecoverySnapshot& snapshot, const std::string& event,
                                     uint64_t bytes, uint64_t transfer) = 0;
    virtual ProtectionTransferDispatcher& Transfers() = 0;
    BackupStoragePool& Pool(uint32_t node) { return *Pools().at(node); }
    void QueueRecovery(ProtectionTransferKey key, uint32_t source, uint32_t destination,
                       uint64_t bytes, uint64_t work, uint64_t object,
                       std::function<bool()> live, std::function<void(uint64_t)> registered);
};
} // namespace ns3::protection
#endif
