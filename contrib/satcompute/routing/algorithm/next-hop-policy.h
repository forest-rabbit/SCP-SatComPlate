/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NEXT_HOP_POLICY_H
#define SATCOMPUTE_NEXT_HOP_POLICY_H

#include "../common/ecmp-flow-key.h"
#include "../common/ecmp-route-candidate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{

struct NextHopSelectionContext
{
    uint32_t nodeId;
    uint64_t routeEpoch;
    uint64_t hashSeed;
    EcmpFlowKey flowKey;
};

struct NextHopDecision
{
    bool useNativeGlobalRouting;
    uint32_t candidateIndex;
    uint64_t score;
    std::string selectionReason;
};

class NextHopPolicy
{
  public:
    virtual ~NextHopPolicy()
    {
    }

    virtual NextHopDecision Select(const NextHopSelectionContext& context,
                                   const std::vector<EcmpRouteCandidate>& candidates) = 0;
};

} // namespace ns3

#endif
