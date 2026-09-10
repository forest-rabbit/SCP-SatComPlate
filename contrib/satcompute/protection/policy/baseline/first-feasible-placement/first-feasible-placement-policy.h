/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FIRST_FEASIBLE_PLACEMENT_POLICY_H
#define SATCOMPUTE_FIRST_FEASIBLE_PLACEMENT_POLICY_H
#include "../../placement-policy.h"

namespace ns3::protection
{
/** FFP: smallest feasible stable ID for local, then for distinct remote. */
class FirstFeasiblePlacementPolicy : public PlacementPolicy
{
  public:
    std::optional<PlacementDecision> Select(const PlacementContext& context) const override;
};
} // namespace ns3::protection
#endif
