/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_ONE_PLUS_ONE_POLICY_H
#define SATCOMPUTE_ONE_PLUS_ONE_POLICY_H
#include "../../../runtime/protection-runtime.h"
#include "../../placement-policy.h"
#include <set>

namespace ns3::protection
{
/** One resource-constrained request per first TASK_RUNNING, with no admission retry. */
class OnePlusOnePolicy : public ProtectionPolicy
{
  public:
    explicit OnePlusOnePolicy(PlacementPolicy& placement) : m_placement(placement) {}
    PlacementPolicy& Placement() { return m_placement; }
    ProtectionAction OnTaskComputeStart(const ProtectionContext& context) override
    {
        if (context.attempt.generation || !context.firstComputeStart ||
            !m_requested.insert(context.attempt.taskId).second)
            return {};
        const auto node = m_placement.SelectBackupNode(
            {context.primaryNode, context.candidates}, context.backupNodeFeasible);
        m_placement.RecordSelection({context.attempt.taskId, context.nowNs, context.primaryNode,
            {}, node, "NOT_REQUESTED", node ? "SELECTED" : "NO_CANDIDATE"});
        return node ? ProtectionAction{ActionKind::START_REPLICA, std::nullopt, node}
                    : ProtectionAction{};
    }
    ProtectionAction OnProtectionEpoch(const ProtectionContext&) override { return {}; }
    ProtectionAction OnComputeFault(const ProtectionContext&) override { return {}; }
    void OnTaskComputeComplete(AttemptKey) override {}
    void OnTaskTerminal(uint64_t) override {}

  private:
    PlacementPolicy& m_placement; ///< Common injected single-node policy.
    std::set<uint64_t> m_requested; ///< Lifetime one-shot guard, including rejected requests.
};
} // namespace ns3::protection
#endif
