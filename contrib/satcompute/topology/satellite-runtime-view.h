/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_RUNTIME_VIEW_H
#define SATCOMPUTE_SATELLITE_RUNTIME_VIEW_H

#include "../routing/algorithm/capacity-aware-path-view.h"
#include "../routing/state/flow-route-registry.h"
#include "satellite-endpoint-view.h"

#include "ns3/callback.h"
#include "ns3/node.h"
#include "ns3/ptr.h"

#include <cstdint>

namespace ns3
{

/** Runtime services shared by replay and online satellite controllers. */
class SatelliteRuntimeView : public CapacityAwarePathView, public SatelliteEndpointView
{
  public:
    virtual ~SatelliteRuntimeView()
    {
    }

    virtual Ptr<Node> GetNodeBySatelliteId(uint32_t satelliteId) const = 0;
    virtual Ptr<FlowRouteRegistry> GetFlowRouteRegistry() const = 0;
    virtual void RegisterRouteUpdateCallback(Callback<void> callback) = 0;
    virtual void InvalidateFlowRouteDecisionCache(const EcmpFlowKey& flowKey) const = 0;
    virtual uint64_t GetRouteEpoch(uint32_t satelliteId) const = 0;
    virtual uint64_t GetHashSeed() const = 0;
    virtual bool IsCapacityAwareRouting() const = 0;
};

} // namespace ns3

#endif
