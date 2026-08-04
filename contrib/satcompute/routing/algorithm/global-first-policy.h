/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_GLOBAL_FIRST_POLICY_H
#define SATCOMPUTE_GLOBAL_FIRST_POLICY_H

#include "next-hop-policy.h"

namespace ns3
{

class GlobalFirstPolicy : public NextHopPolicy
{
  public:
    NextHopDecision Select(const NextHopSelectionContext& context,
                           const std::vector<EcmpRouteCandidate>& candidates) override;
};

} // namespace ns3

#endif
