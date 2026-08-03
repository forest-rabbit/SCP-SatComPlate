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

// 表示完全委托给 ns-3 Ipv4GlobalRouting 的原生首条路由行为。

#include "global-first-policy.h"

namespace ns3 {

NextHopDecision
GlobalFirstPolicy::Select(
  const NextHopSelectionContext&,
  const std::vector<EcmpRouteCandidate>&)
{
  return {
    true,
    0,
    0,
    "GLOBAL_FIRST_NATIVE"
  };
}

} // namespace ns3
