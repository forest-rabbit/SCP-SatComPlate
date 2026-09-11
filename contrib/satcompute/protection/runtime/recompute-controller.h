/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RECOMPUTE_CONTROLLER_H
#define SATCOMPUTE_RECOMPUTE_CONTROLLER_H
#include "recovery-controller.h"
#include "../policy/baseline/recompute/recompute-policy.h"
#include "../policy/placement-policy.h"

namespace ns3::protection
{
/** Full Recompute scheme, using shared recovery without any checkpoint objects. */
class RecomputeController
{
  public:
    /** Bind a single-node placement policy; zero-capacity ledgers never allocate backup bytes. */
    RecomputeController(Ptr<TaskCoordinator> tasks,
                        SatelliteRuntimeView& topology,
                        int64_t stopNs,
                        std::unique_ptr<PlacementPolicy> placement);
    /** Finalize logical tasks, recovery and the empty checkpoint inventory. */
    void Finalize();
    /** Shared transfer/fault ledgers; checkpoint summaries and allocated bytes stay empty. */
    const CheckpointManager& Manager() const { return m_manager; }
    /** Same actual recovery ledger as checkpoint schemes. */
    const RecoveryController& Recovery() const { return m_recovery; }

  private:
    Ptr<TaskCoordinator> m_tasks; ///< Shared logical task owner.
    std::unique_ptr<PlacementPolicy> m_placement; ///< Injected single-node ranking.
    RecomputePolicy m_policy; ///< No checkpoint actions.
    CheckpointManager m_manager; ///< Shared canonical flow allocator; no checkpoint state created.
    RecoveryController m_recovery; ///< Shared real INPUT/compute/RESULT executor.
};
} // namespace ns3::protection
#endif
