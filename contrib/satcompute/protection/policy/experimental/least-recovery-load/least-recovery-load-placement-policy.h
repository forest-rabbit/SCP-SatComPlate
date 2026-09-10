/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_LEAST_RECOVERY_LOAD_PLACEMENT_POLICY_H
#define SATCOMPUTE_LEAST_RECOVERY_LOAD_PLACEMENT_POLICY_H
#include "../../placement-policy.h"

namespace ns3::protection
{
/** Diagnostic only: assignment count + weight * active recovery count, then stable ID. */
class LeastRecoveryLoadPlacementPolicy : public PlacementPolicy
{
  public:
    /** Explicit nonnegative integer weight; no production parameter or default. */
    explicit LeastRecoveryLoadPlacementPolicy(uint32_t recoveryWeight);
    std::optional<PlacementDecision> Select(const PlacementContext& context) const override;

  private:
    uint32_t m_recoveryWeight; ///< Diagnostic weight, never an N5B main-scene setting.
};
} // namespace ns3::protection
#endif
