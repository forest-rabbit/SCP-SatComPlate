/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_MULTITREE_CONTROLLER_H
#define SATCOMPUTE_MULTITREE_CONTROLLER_H
#include "multitree-decision-log.h"
#include "../recompute/recompute-runtime.h"
#include "../one-plus-one/one-plus-one-policy.h"
#include "../../mechanism/replication/replica-manager.h"
namespace ns3::protection::multitree
{
/** Sole mixed-scheme hook owner; FT tree chooses RS/RP, shared mechanisms execute it. */
class MultiTreeController
{
  public:
    MultiTreeController(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                        Ptr<FaultModelEngine> faults, int64_t stopNs,
                        std::unique_ptr<PlacementPolicy> placement);
    ~MultiTreeController();
    void Finalize();
    void WriteMetrics(const std::filesystem::path& directory) const;
    const DecisionLog& Decisions() const { return m_decisions; }
    const RecomputeController& Resubmission() const { return m_recompute; }
    const ReplicaManager& Replication() const { return m_replication; }

  private:
    void OnTask(const TaskEventRecord& event);
    Ptr<TaskCoordinator> m_tasks; ///< One logical task/deadline owner.
    DecisionLog m_decisions; ///< Immutable first-start features and published rule result.
    RecomputeController m_recompute; ///< Shared native RS executor with auto-hook disabled.
    OnePlusOnePolicy m_replicaRequest; ///< Reused one-shot RP node admission, not the FT decision.
    ReplicaManager m_replication; ///< Shared physical replica; no second controller/ID stream.
    bool m_finalized{}; ///< Prevent duplicate finalization/evidence cleanup.
};
} // namespace ns3::protection::multitree
#endif
