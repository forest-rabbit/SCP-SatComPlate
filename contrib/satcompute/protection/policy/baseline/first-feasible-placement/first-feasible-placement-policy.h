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
    const char* Name() const override { return "ffp"; }
    void RankBackupNodes(std::vector<uint32_t>& nodes, const PlacementContext&) const override;
    void RankPairs(std::vector<PlacementDecision>& pairs, const PlacementContext&) const override;
};
} // namespace ns3::protection
#endif
