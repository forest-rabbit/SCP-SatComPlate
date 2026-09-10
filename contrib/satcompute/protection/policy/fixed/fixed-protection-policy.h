/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FIXED_PROTECTION_POLICY_H
#define SATCOMPUTE_FIXED_PROTECTION_POLICY_H
#include "../../runtime/protection-runtime.h"
#include <set>

namespace ns3::protection
{
/** Deterministic fixture policy, not CompFRR optimization or admission reservation. */
class FixedProtectionPolicy : public ProtectionPolicy
{
  public:
    /** Use explicit frequency from typed configuration, never manager defaults. */
    FixedProtectionPolicy(uint32_t deltaPermille, uint32_t batchN);
    ProtectionAction OnTaskComputeStart(const ProtectionContext& context) override;
    ProtectionAction OnProtectionEpoch(const ProtectionContext& context) override;
    ProtectionAction OnComputeFault(const ProtectionContext& context) override;
    void OnTaskComputeComplete(AttemptKey attempt) override;
    void OnTaskTerminal(uint64_t taskId) override;

  private:
    uint32_t m_delta;             ///< Fixed interval in per mille.
    uint32_t m_batchN;            ///< Fixed batch count.
    std::set<uint64_t> m_started; ///< Exactly-once first-dispatch policy bookkeeping.
};
} // namespace ns3::protection
#endif
