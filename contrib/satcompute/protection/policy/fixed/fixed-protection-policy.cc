/* SPDX-License-Identifier: GPL-2.0-only */
#include "fixed-protection-policy.h"
#include <stdexcept>

namespace ns3::protection
{
FixedProtectionPolicy::FixedProtectionPolicy(uint32_t deltaPermille, uint32_t batchN,
                                             std::unique_ptr<PlacementPolicy> placement)
    : m_placement(placement ? std::move(placement) : std::make_unique<FaFirstFeasiblePlacementPolicy>()),
      m_delta(deltaPermille), m_batchN(batchN)
{
    if (!m_delta || m_delta > 1000 || !m_batchN || m_batchN > 1000 / m_delta)
        throw std::invalid_argument("fixed checkpoint requires delta>0, n>0, n*delta<=1");
}

ProtectionAction
FixedProtectionPolicy::OnTaskComputeStart(const ProtectionContext& context)
{
    if (!context.attempt.taskId || context.attempt.generation != 0 || !context.firstComputeStart ||
        !context.taskSelected || context.phase != ProtectionPhase::OFF ||
        !m_started.insert(context.attempt.taskId).second)
        return {};
    const auto pair = m_placement->SelectCheckpointPair(
        {context.primaryNode, context.candidates}, context.previewPath);
    m_placement->RecordSelection({context.attempt.taskId, context.nowNs, context.primaryNode,
        pair, {}, "NOT_REQUESTED", pair ? "SELECTED" : "NO_CANDIDATE"});
    if (!pair)
        return {};
    if (m_placement->Eligibility() == PlacementEligibility::MINIMAL)
    {
        // Validate this selected pair only; no search, no additional Fixed frequency rule.
        for (const auto [source, destination] :
             {std::pair{context.primaryNode, pair->localNode},
              std::pair{context.primaryNode, pair->remoteNode},
              std::pair{pair->localNode, pair->remoteNode}})
        {
            const auto path = context.previewPath ? context.previewPath(source, destination)
                : PlacementPathAvailability{true, true, ""};
            if (!path.reachable || !path.admissible)
            {
                m_placement->RecordAdmission(context.attempt.taskId, context.nowNs, "REJECTED",
                                             path.reachable ? path.reason : "NO_ROUTE");
                return {};
            }
        }
    }
    m_placement->RecordAdmission(context.attempt.taskId, context.nowNs, "REQUESTED", "CHECKPOINT_START");
    return {ActionKind::START_CHECKPOINT,
            CheckpointConfiguration{m_delta, m_batchN, pair->localNode, pair->remoteNode}};
}

ProtectionAction
FixedProtectionPolicy::OnProtectionEpoch(const ProtectionContext&)
{
    return {};
}

ProtectionAction
FixedProtectionPolicy::OnComputeFault(const ProtectionContext& context)
{
    // Runtime offered the existing checkpoint mechanism recovery before reaching fallback.
    if (!context.attempt.taskId || context.attempt.generation != 0 ||
        context.phase == ProtectionPhase::DONE || context.phase == ProtectionPhase::RECOVERING)
        return {};
    return {ActionKind::RECOMPUTE, std::nullopt};
}

void
FixedProtectionPolicy::OnTaskComputeComplete(AttemptKey)
{
}

void
FixedProtectionPolicy::OnTaskTerminal(uint64_t taskId)
{
    m_started.erase(taskId);
}
} // namespace ns3::protection
