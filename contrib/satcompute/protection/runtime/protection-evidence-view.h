/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_EVIDENCE_VIEW_H
#define SATCOMPUTE_PROTECTION_EVIDENCE_VIEW_H
#include "../common/checkpoint-evidence.h"
#include "../storage/backup-storage-pool.h"
#include "protection-transfer-dispatcher.h"
#include <memory>
namespace ns3::protection
{
using BackupPools = std::map<uint32_t, std::unique_ptr<BackupStoragePool>>;
/** Read-only external metrics contract; empty checkpoint evidence is legal for baselines. */
class ProtectionEvidenceView
{
  public:
    virtual ~ProtectionEvidenceView() = default;
    virtual InputStagingPolicy InputPolicy() const = 0;
    virtual uint64_t GlobalStoragePeakBytes() const = 0;
    virtual const std::vector<ProtectionEvent>& Events() const = 0;
    virtual const std::vector<ProtectionFlow>& Flows() const = 0;
    virtual std::vector<ProtectionTaskSummary> Summaries() const = 0;
    virtual bool IsQuiescent() const = 0;
    virtual const BackupPools& Pools() const = 0;
};
} // namespace ns3::protection
#endif
