/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-runtime.h"
#include <set>
#include <stdexcept>
#include <utility>

namespace ns3::protection
{
ProtectionRuntime::ProtectionRuntime(ProtectionPolicy& policy,
                                     std::vector<ProtectionMechanism*> mechanisms)
    : m_policy(policy), m_mechanisms(std::move(mechanisms))
{
    std::set<ProtectionMechanism*> seen;
    for (auto* mechanism : m_mechanisms)
        if (!mechanism || !seen.insert(mechanism).second)
            throw std::invalid_argument("null or duplicate protection mechanism");
}

void
ProtectionRuntime::Dispatch(const ProtectionContext& context, const ProtectionAction& action)
{
    if (action.kind == ActionKind::NONE)
        return;
    ProtectionMechanism* selected = nullptr;
    for (auto* mechanism : m_mechanisms)
        if (mechanism->Supports(action.kind))
        {
            if (selected)
                throw std::logic_error("ambiguous protection action handler");
            selected = mechanism;
        }
    if (!selected)
        throw std::logic_error("protection action has no implementation");
    selected->Execute(context, action);
}

void
ProtectionRuntime::OnTaskComputeStart(const ProtectionContext& context)
{
    Dispatch(context, m_policy.OnTaskComputeStart(context));
}

void
ProtectionRuntime::OnProtectionEpoch(const ProtectionContext& context)
{
    Dispatch(context, m_policy.OnProtectionEpoch(context));
}

void
ProtectionRuntime::OnComputeFault(const ProtectionContext& context)
{
    for (auto* mechanism : m_mechanisms)
        if (mechanism->OnComputeFault(context))
            return;
    Dispatch(context, m_policy.OnComputeFault(context));
}

void
ProtectionRuntime::OnTaskComputeComplete(AttemptKey attempt)
{
    m_policy.OnTaskComputeComplete(attempt);
    for (auto* mechanism : m_mechanisms)
        mechanism->OnTaskComputeComplete(attempt);
}

void
ProtectionRuntime::OnTaskTerminal(uint64_t taskId)
{
    m_policy.OnTaskTerminal(taskId);
    for (auto* mechanism : m_mechanisms)
        mechanism->OnTaskTerminal(taskId);
}
} // namespace ns3::protection
