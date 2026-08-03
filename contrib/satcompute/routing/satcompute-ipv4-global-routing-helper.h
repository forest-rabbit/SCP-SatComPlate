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

#ifndef SATCOMPUTE_IPV4_GLOBAL_ROUTING_HELPER_H
#define SATCOMPUTE_IPV4_GLOBAL_ROUTING_HELPER_H

#include "satcompute-ipv4-global-routing.h"

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"

#include <cstdint>

namespace ns3 {

class SatComputeIpv4GlobalRoutingHelper : public Ipv4RoutingHelper
{
public:
  SatComputeIpv4GlobalRoutingHelper(
    RoutingMode selectionMode = RoutingMode::GLOBAL_FIRST,
    uint64_t hashSeed = 1,
    Ptr<SizeAwareFlowRegistry> sizeAwareRegistry = nullptr);
  SatComputeIpv4GlobalRoutingHelper(
    const SatComputeIpv4GlobalRoutingHelper& other);

  SatComputeIpv4GlobalRoutingHelper* Copy() const override;
  Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

  static Ptr<SatComputeIpv4GlobalRouting> GetRouting(Ptr<Node> node);
  static void AdvanceRouteEpoch(const NodeContainer& nodes);
  static void InvalidateDecisionCache(const NodeContainer& nodes,
                                      const EcmpFlowKey& flowKey);

private:
  RoutingMode m_selectionMode;
  uint64_t m_hashSeed;
  Ptr<SizeAwareFlowRegistry> m_sizeAwareRegistry;
};

} // namespace ns3

#endif
