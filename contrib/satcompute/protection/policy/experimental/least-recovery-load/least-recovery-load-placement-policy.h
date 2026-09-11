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
    const char* Name() const override { return "lrl"; }
    /** Explicit nonnegative integer weight, frozen before a diagnostic experiment. */
    explicit LeastRecoveryLoadPlacementPolicy(uint32_t recoveryWeight);
    void RankBackupNodes(std::vector<uint32_t>& nodes,
                         const PlacementContext& context) const override;
    void RankPairs(std::vector<PlacementDecision>& pairs,
                   const PlacementContext& context) const override;

  private:
    uint32_t m_recoveryWeight; ///< Diagnostic weight, never an N5B main-scene setting.
};
} // namespace ns3::protection
#endif
