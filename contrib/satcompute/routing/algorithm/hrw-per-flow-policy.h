/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SATCOMPUTE_HRW_PER_FLOW_POLICY_H
#define SATCOMPUTE_HRW_PER_FLOW_POLICY_H

#include "next-hop-policy.h"

#include <array>

namespace ns3 {

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

std::array<uint8_t, 37> EncodeEcmpHrwKey(
  uint64_t hashSeed,
  const EcmpFlowKey& flowKey,
  const EcmpRouteCandidate& candidate);
uint64_t ScoreEcmpHrwRoute(
  uint64_t hashSeed,
  const EcmpFlowKey& flowKey,
  const EcmpRouteCandidate& candidate);
EcmpHrwSelection SelectEcmpHrwRoute(
  uint64_t hashSeed,
  const EcmpFlowKey& flowKey,
  const std::vector<EcmpRouteCandidate>& candidates);
std::vector<EcmpHrwRank> RankEcmpHrwRoutes(
  uint64_t hashSeed,
  const EcmpFlowKey& flowKey,
  const std::vector<EcmpRouteCandidate>& candidates);

class HrwPerFlowPolicy : public NextHopPolicy
{
public:
  NextHopDecision Select(
    const NextHopSelectionContext& context,
    const std::vector<EcmpRouteCandidate>& candidates) override;
};

} // namespace ns3

#endif
