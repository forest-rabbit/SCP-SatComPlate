/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RECOMPUTE_CONTROLLER_H
#define SATCOMPUTE_RECOMPUTE_CONTROLLER_H
#include "../../../runtime/recovery-controller.h"
#include "../../../runtime/transfer-only-recovery-ledger.h"
#include "recompute-policy.h"
#include "../../placement-policy.h"

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
    const TransferOnlyRecoveryLedger& Manager() const { return m_manager; }
    /** Same actual recovery ledger as checkpoint schemes. */
    const RecoveryController& Recovery() const { return m_recovery; }
    const PlacementPolicy& Placement() const { return *m_placement; }
    const PlacementLoadLedger& PlacementLoads() const { return m_loads; }

  private:
    Ptr<TaskCoordinator> m_tasks; ///< Shared logical task owner.
    std::unique_ptr<PlacementPolicy> m_placement; ///< Injected single-node ranking.
    PlacementLoadLedger m_loads; ///< Real accepted native R0 recovery ownership.
    RecomputePolicy m_policy; ///< No checkpoint actions.
    TransferOnlyRecoveryLedger m_manager; ///< No checkpoint executor; only shared transport/evidence.
    RecoveryController m_recovery; ///< Shared real INPUT/compute/RESULT executor.
};
} // namespace ns3::protection
#endif
