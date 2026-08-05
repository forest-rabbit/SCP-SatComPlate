/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Subscribe to per-flow route decisions before simulation traffic starts.

#include "ecmp-route-recorder.h"

#include "../../routing/ns3/satcompute-ipv4-global-routing-helper.h"
#include "../../topology/satellite-id-map.h"
#include "../../topology/satellite-topology.h"
#include "../../topology/satellite-topology-controller.h"

#include "ns3/callback.h"
#include "ns3/node-container.h"

#include <stdexcept>

namespace ns3
{

EcmpRouteRecorder::EcmpRouteRecorder(const SatelliteTopology& topology)
{
    Attach(topology);
}

EcmpRouteRecorder::EcmpRouteRecorder(const SatelliteTopologyController& topology)
{
    Attach(topology);
}

void
EcmpRouteRecorder::Attach(const SatelliteTopologyController& topology)
{
    const NodeContainer& nodes = topology.GetNodes();
    const SatelliteIdMap& idMap = topology.GetIdMap();
    for (uint32_t index = 0; index < idMap.GetNodeCount(); ++index)
    {
        const bool connected =
            SatComputeIpv4GlobalRoutingHelper::GetRouting(nodes.Get(index))
                ->TraceConnectWithoutContext("EcmpRouteDecision",
                                             MakeCallback(&EcmpRouteRecorder::Record, this));
        if (!connected)
        {
            throw std::runtime_error("cannot connect EcmpRouteDecision for satellite " +
                                     std::to_string(idMap.GetSatelliteIdByNodeIndex(index)));
        }
    }
}

const std::vector<EcmpRouteDecisionEvent>&
EcmpRouteRecorder::GetEvents() const
{
    return m_events;
}

void
EcmpRouteRecorder::Record(const EcmpRouteDecisionEvent& event)
{
    m_events.push_back(event);
}

} // namespace ns3
