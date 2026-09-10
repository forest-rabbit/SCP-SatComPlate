/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FIXED_PROTECTION_CONTROLLER_H
#define SATCOMPUTE_FIXED_PROTECTION_CONTROLLER_H
#include "../mechanism/checkpoint/checkpoint-manager.h"
#include "../policy/fixed/fixed-protection-policy.h"

namespace ns3::protection
{
/** Read-only primary task-event adapter; G2 does not intercept faults or recovery. */
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
                              uint32_t batchN);
    ~FixedProtectionController();
    void Finalize(); ///< Release remaining protection after simulation stop.

    /** @return Mechanism evidence for the dedicated metrics writer. */
    const CheckpointManager& Manager() const
    {
        return m_manager;
    }

  private:
    void OnTask(const TaskEventRecord& event); ///< Observe, never mutate ordinary task state.
    Ptr<TaskCoordinator> m_tasks;              ///< Retained coordinator, outlives event binding.
    SatelliteRuntimeView& m_topology;          ///< Existing real network view.
    CheckpointManager m_manager;               ///< Sole G2 checkpoint executor.
    FixedProtectionPolicy m_policy;            ///< Explicit fixed policy, no probability query.
    ProtectionRuntime m_runtime;               ///< Policy/mechanism dispatcher.
};
} // namespace ns3::protection
#endif
