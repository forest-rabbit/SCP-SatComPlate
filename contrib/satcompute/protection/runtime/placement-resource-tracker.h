/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PLACEMENT_RESOURCE_TRACKER_H
#define SATCOMPUTE_PLACEMENT_RESOURCE_TRACKER_H
#include "../common/placement-resources.h"
#include "../storage/peak-quota-ledger.h"
#include "../mechanism/checkpoint/checkpoint-manager.h"
#include "../../fault/runtime/fault-model-engine.h"
#include "compute-usage-history.h"
#include <filesystem>

namespace ns3::protection
{
/** Causal ledger/diagnostics only. Never owns compute, transfers, fault samples or Frequency. */
class PlacementResourceTracker
{
  public:
    PlacementResourceTracker(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults,
                        CheckpointManager& manager, int64_t stopNs);
    virtual ~PlacementResourceTracker();
    void FillResources(PlacementResourceSnapshot& candidate, int64_t remainingTimeNs) const;
    uint64_t FreeFor(uint32_t node, uint64_t replacingTask) const;
    /** Physical growth allowed by other owners' promises and this owner's committed peak. */
    uint64_t MaintenanceFree(uint32_t node, uint64_t task) const;
    uint64_t PeakFor(uint32_t node, uint64_t task, uint64_t additional) const;
    bool CanCommit(uint64_t task, uint32_t node, uint64_t peak) const;
    void CommitQuota(uint64_t task, uint32_t node, uint64_t peak);
    void ReleaseQuota(uint64_t task);
    void Assignment(uint64_t task, uint32_t node, bool active);
    void Initialized(uint64_t task);
    std::optional<int64_t> ReadyAfter(uint64_t task) const;
    const std::map<uint32_t, PlacementNodeObservation>& Nodes() const { return m_nodes; }
    bool QuotasEmpty() const { return m_quotas.Empty(); }
    void Finalize();
    void WriteResources(const std::filesystem::path& directory) const;
  private:
    void ObserveStorage(uint32_t node);
    /** Passive service notification; never schedules or reserves compute. */
    void ObserveCompute(uint32_t node, bool busy);
    /** Check the event ledger against the preexisting cumulative service counters. */
    void VerifyHistory(uint32_t node) const;
    Ptr<ComputeService> Service(uint32_t node) const;
    Ptr<TaskCoordinator> m_tasks;
    Ptr<FaultModelEngine> m_faults;
    CheckpointManager& m_manager;
    int64_t m_stopNs;
    PeakQuotaLedger m_quotas;
    ComputeUsageHistory m_computeHistory; ///< Actual normal/recovery service intervals only.
    std::map<uint32_t, Ptr<ComputeService>> m_services; ///< Observer lifetime owners.
    std::map<uint32_t, PlacementNodeObservation> m_nodes;
    std::map<uint64_t, uint32_t> m_assignments; ///< Match idempotent shared-runtime release callbacks.
    std::map<uint64_t, int64_t> m_ready;
    bool m_finalized{};
};
} // namespace ns3::protection
#endif
