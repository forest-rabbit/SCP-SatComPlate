/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FA_LEAST_RECOVERY_LOAD_PLACEMENT_POLICY_H
#define SATCOMPUTE_FA_LEAST_RECOVERY_LOAD_PLACEMENT_POLICY_H
#include "../least-recovery-load/least-recovery-load-placement-policy.h"

namespace ns3::protection
{
/** Historical LRL: same FA candidate set as FA-FFP; only load ranking differs. */
class FaLeastRecoveryLoadPlacementPolicy : public LeastRecoveryLoadPlacementPolicy
{
  public:
    explicit FaLeastRecoveryLoadPlacementPolicy(uint32_t weight) : LeastRecoveryLoadPlacementPolicy(weight) {}
    const char* Name() const override { return "fa-lrl"; }
    PlacementEligibility Eligibility() const override { return PlacementEligibility::FEASIBILITY_AWARE; }
};
} // namespace ns3::protection
#endif
