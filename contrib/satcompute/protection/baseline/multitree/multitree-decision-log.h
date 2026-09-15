/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_MULTITREE_DECISION_LOG_H
#define SATCOMPUTE_MULTITREE_DECISION_LOG_H
#include "multitree-published-rule.h"
#include "../../../task/task-coordinator.h"
#include "../../../fault/runtime/fault-model-engine.h"
namespace ns3::protection::multitree
{
/** Read-only decision snapshot, shared by passive preflight and physical controller. */
class DecisionLog
{
  public:
    DecisionLog(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults);
    /** Capture once at first primary TASK_RUNNING, before physical admission. */
    RuleResult Capture(uint64_t taskId);
    Decision Get(uint64_t taskId) const;
    /** Separate files; never rewrites another baseline's output schema. */
    void Write(const std::filesystem::path& directory) const;

  private:
    struct Row
    {
        Features features; ///< Causal values at first primary start.
        RuleResult rule; ///< Frozen published leaf.
        int64_t timeNs{}; ///< Actual decision time.
    };
    Ptr<TaskCoordinator> m_tasks; ///< Stable task/queue owners.
    Ptr<FaultModelEngine> m_faults; ///< Read-only canonical current probability adapter.
    Calibration m_scale; ///< One frozen artifact.
    std::map<uint64_t, const TaskRuntime*> m_byId; ///< Stable task references.
    std::map<uint64_t, uint64_t> m_bytes; ///< Immutable INPUT sizes.
    std::map<uint64_t, Row> m_rows; ///< Exactly one row per decided task.
};
} // namespace ns3::protection::multitree
#endif
