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

#ifndef SATCOMPUTE_SIZE_AWARE_HRW_POLICY_H
#define SATCOMPUTE_SIZE_AWARE_HRW_POLICY_H

#include "next-hop-policy.h"
#include "../state/size-aware-routing-state.h"

namespace ns3 {

class SizeAwareHrwPolicy : public NextHopPolicy
{
public:
  explicit SizeAwareHrwPolicy(SizeAwareRoutingState& state);

  NextHopDecision Select(
    const NextHopSelectionContext& context,
    const std::vector<EcmpRouteCandidate>& candidates) override;

private:
  SizeAwareRoutingState* m_state;
};

} // namespace ns3

#endif
