/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_HRW_PER_FLOW_POLICY_H
#define SATCOMPUTE_HRW_PER_FLOW_POLICY_H

#include "next-hop-policy.h"

#include <array>

namespace ns3
{

struct EcmpHrwSelection
{
    uint32_t candidateIndex;
    uint64_t score;
};

struct EcmpHrwRank
{
    uint32_t candidateIndex;
    uint64_t score;
};

std::array<uint8_t, 37> EncodeEcmpHrwKey(uint64_t hashSeed,
                                         const EcmpFlowKey& flowKey,
                                         const EcmpRouteCandidate& candidate);
uint64_t ScoreEcmpHrwRoute(uint64_t hashSeed,
                           const EcmpFlowKey& flowKey,
                           const EcmpRouteCandidate& candidate);
EcmpHrwSelection SelectEcmpHrwRoute(uint64_t hashSeed,
                                    const EcmpFlowKey& flowKey,
                                    const std::vector<EcmpRouteCandidate>& candidates);
std::vector<EcmpHrwRank> RankEcmpHrwRoutes(uint64_t hashSeed,
                                           const EcmpFlowKey& flowKey,
                                           const std::vector<EcmpRouteCandidate>& candidates);

class HrwPerFlowPolicy : public NextHopPolicy
{
  public:
    NextHopDecision Select(const NextHopSelectionContext& context,
                           const std::vector<EcmpRouteCandidate>& candidates) override;
};

} // namespace ns3

#endif
