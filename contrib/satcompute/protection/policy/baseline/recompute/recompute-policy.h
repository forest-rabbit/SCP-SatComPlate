/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RECOMPUTE_POLICY_H
#define SATCOMPUTE_RECOMPUTE_POLICY_H
#include "../../../runtime/protection-runtime.h"

namespace ns3::protection
{
/** No prefault activity; a primary interruption requests from-zero execution. */
class RecomputePolicy : public ProtectionPolicy
{
  public:
    ProtectionAction OnTaskComputeStart(const ProtectionContext&) override { return {}; }
    ProtectionAction OnProtectionEpoch(const ProtectionContext&) override { return {}; }
    ProtectionAction OnComputeFault(const ProtectionContext& context) override
    {
        return context.attempt.generation == 0
                   ? ProtectionAction{ActionKind::RECOMPUTE, std::nullopt}
                   : ProtectionAction{};
    }
    void OnTaskComputeComplete(AttemptKey) override {}
    void OnTaskTerminal(uint64_t) override {}
};
} // namespace ns3::protection
#endif
