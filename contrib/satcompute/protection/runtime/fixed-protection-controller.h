/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FIXED_PROTECTION_CONTROLLER_H
#define SATCOMPUTE_FIXED_PROTECTION_CONTROLLER_H
#include "../mechanism/checkpoint/checkpoint-manager.h"
#include "../policy/fixed/fixed-protection-policy.h"
#include "recovery-controller.h"
#include "placement-load-ledger.h"

namespace ns3::protection
{
/** Fixed-policy primary event adapter with optional fault-enabled recovery arbitration. */
class FixedProtectionController
{
  public:
    /** Bind fixed placement/frequency and shared storage to existing task transitions.
     * @param tasks Existing primary task coordinator.
     * @param topology Current route/candidate view.
     * @param capacity Explicit extra-storage bytes per compute node.
     * @param stopNs Absolute simulation stop in ns.
     * @param deltaPermille Checkpoint interval in per mille.
     * @param batchN Contiguous local records per remote batch.
     */
    FixedProtectionController(Ptr<TaskCoordinator> tasks,
                              SatelliteRuntimeView& topology,
                              uint64_t capacity,
                              int64_t stopNs,
                              uint32_t deltaPermille,
                              uint32_t batchN,
                              bool enableRecovery = false,
                              std::unique_ptr<PlacementPolicy> placement = nullptr,
                              RemoteBusyRecoveryPolicy busyPolicy = RemoteBusyRecoveryPolicy::RELOCATE);
    ~FixedProtectionController();
    void Finalize(); ///< Release remaining protection after simulation stop.
    const PlacementLoadLedger& PlacementLoads() const { return m_loads; } ///< Actual ownership.
    const PlacementPolicy& Placement() const { return m_policy.Placement(); }

    /** @return Mechanism evidence for the dedicated metrics writer. */
    const CheckpointManager& Manager() const
    {
        return m_manager;
    }

    /** Optional fault-enabled G3 recovery evidence; null in the no-fault G2 path. */
    const RecoveryController* Recovery() const
    {
        return m_recovery.get();
    }

  private:
    void OnTask(const TaskEventRecord& event); ///< Observe, never mutate ordinary task state.
    Ptr<TaskCoordinator> m_tasks;              ///< Retained coordinator, outlives event binding.
    SatelliteRuntimeView& m_topology;          ///< Existing real network view.
    PlacementLoadLedger m_loads;               ///< Shared ownership snapshot for FFP/LRL.
    CheckpointManager m_manager;               ///< Sole G2 checkpoint executor.
    FixedProtectionPolicy m_policy;            ///< Explicit fixed policy, no probability query.
    ProtectionRuntime m_runtime;               ///< Policy/mechanism dispatcher.
    std::unique_ptr<RecoveryController> m_recovery; ///< Optional G3 fault/recovery adapter.
};
} // namespace ns3::protection
#endif
