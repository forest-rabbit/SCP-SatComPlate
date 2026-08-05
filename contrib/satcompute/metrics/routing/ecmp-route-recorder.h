/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ECMP_ROUTE_RECORDER_H
#define SATCOMPUTE_ECMP_ROUTE_RECORDER_H

#include "../../routing/ns3/satcompute-ipv4-global-routing.h"

#include <vector>

namespace ns3
{

class SatelliteTopology;
class SatelliteTopologyController;

/** Subscribe to every satellite routing instance and retain route decisions. */
class EcmpRouteRecorder
{
  public:
    explicit EcmpRouteRecorder(const SatelliteTopology& topology);
    explicit EcmpRouteRecorder(const SatelliteTopologyController& topology);

    const std::vector<EcmpRouteDecisionEvent>& GetEvents() const;

  private:
    void Attach(const SatelliteTopologyController& topology);
    void Record(const EcmpRouteDecisionEvent& event);

    std::vector<EcmpRouteDecisionEvent> m_events;
};

} // namespace ns3

#endif // SATCOMPUTE_ECMP_ROUTE_RECORDER_H
