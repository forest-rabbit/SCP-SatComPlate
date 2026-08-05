/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ROUTING_POLICY_FACTORY_H
#define SATCOMPUTE_ROUTING_POLICY_FACTORY_H

#include "algorithm/capacity-aware-path-view.h"
#include "algorithm/next-hop-policy.h"
#include "algorithm/path-policy.h"
#include "common/routing-mode.h"
#include "state/capacity-reservation-state.h"
#include "state/flow-route-state.h"
#include "state/size-aware-load-view.h"

#include <memory>

namespace ns3
{

/**
 * Construct routing policies behind the stable SatCompute public boundary.
 *
 * Next-hop policies are used by the IPv4 adapter.  Capacity-aware routing is
 * admitted as a complete path, so it is created through CreatePathPolicy().
 */
class RoutingPolicyFactory
{
  public:
    static std::unique_ptr<NextHopPolicy> CreateNextHopPolicy(
        RoutingMode mode,
        FlowRouteState* flowState,
        const SizeAwareLoadView* sizeAwareLoadView);

    static std::unique_ptr<PathPolicy> CreatePathPolicy(
        RoutingMode mode,
        const CapacityAwarePathView* pathView,
        const CapacityReservationState* reservationState);
};

} // namespace ns3

#endif
