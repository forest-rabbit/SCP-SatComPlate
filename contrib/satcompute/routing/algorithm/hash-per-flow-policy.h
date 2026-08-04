/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_HASH_PER_FLOW_POLICY_H
#define SATCOMPUTE_HASH_PER_FLOW_POLICY_H

#include "next-hop-policy.h"

namespace ns3
{

class HashPerFlowPolicy : public NextHopPolicy
{
  public:
    NextHopDecision Select(const NextHopSelectionContext& context,
                           const std::vector<EcmpRouteCandidate>& candidates) override;
};

} // namespace ns3

#endif
