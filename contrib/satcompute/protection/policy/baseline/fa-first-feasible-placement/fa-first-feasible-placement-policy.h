/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FA_FIRST_FEASIBLE_PLACEMENT_POLICY_H
#define SATCOMPUTE_FA_FIRST_FEASIBLE_PLACEMENT_POLICY_H
#include "../first-feasible-placement/first-feasible-placement-policy.h"

namespace ns3::protection
{
/** Historical FFP: system feasibility filtering, unchanged stable-ID ranking. */
class FaFirstFeasiblePlacementPolicy : public FirstFeasiblePlacementPolicy
{
  public:
    const char* Name() const override { return "fa-ffp"; }
    PlacementEligibility Eligibility() const override { return PlacementEligibility::FEASIBILITY_AWARE; }
};
} // namespace ns3::protection
#endif
