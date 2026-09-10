/* SPDX-License-Identifier: GPL-2.0-only */
#include "fixed-protection-policy.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
FixedProtectionPolicy::FixedProtectionPolicy(uint32_t deltaPermille, uint32_t batchN)
    : m_delta(deltaPermille), m_batchN(batchN)
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
    auto nodes = context.candidates;
    std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
        return a.nodeId < b.nodeId;
    });
    const auto feasible = [&](const auto& node) {
        return node.nodeId != context.primaryNode && node.healthy && node.idle && node.reachable;
    };
    auto local = std::find_if(
        nodes.begin(), nodes.end(), [&](const auto& n) { return feasible(n) && n.oneHop; });
    if (local == nodes.end())
        return {};
    auto remote = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) {
        return feasible(n) && n.nodeId != local->nodeId;
    });
    if (remote == nodes.end())
        return {};
    return {ActionKind::START_CHECKPOINT,
            CheckpointConfiguration{m_delta, m_batchN, local->nodeId, remote->nodeId}};
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
