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

#ifndef SATCOMPUTE_NEXT_HOP_POLICY_H
#define SATCOMPUTE_NEXT_HOP_POLICY_H

#include "../common/ecmp-flow-key.h"
#include "../common/ecmp-route-candidate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

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

  virtual NextHopDecision Select(
    const NextHopSelectionContext& context,
    const std::vector<EcmpRouteCandidate>& candidates) = 0;
};

} // namespace ns3

#endif
