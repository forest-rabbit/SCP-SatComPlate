/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_IPV4_GLOBAL_ROUTING_HELPER_H
#define SATCOMPUTE_IPV4_GLOBAL_ROUTING_HELPER_H

#include "satcompute-ipv4-global-routing.h"

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"

#include <cstdint>

namespace ns3
{

class SatComputeIpv4GlobalRoutingHelper : public Ipv4RoutingHelper
{
  public:
    SatComputeIpv4GlobalRoutingHelper(RoutingMode selectionMode = RoutingMode::GLOBAL_FIRST,
                                      uint64_t hashSeed = 1);
    SatComputeIpv4GlobalRoutingHelper(const SatComputeIpv4GlobalRoutingHelper& other);

    SatComputeIpv4GlobalRoutingHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    static Ptr<SatComputeIpv4GlobalRouting> GetRouting(Ptr<Node> node);
    static void AdvanceRouteEpoch(const NodeContainer& nodes);
    static void InvalidateDecisionCache(const NodeContainer& nodes, const EcmpFlowKey& flowKey);

  private:
    RoutingMode m_selectionMode;
    uint64_t m_hashSeed;
};

} // namespace ns3

#endif
