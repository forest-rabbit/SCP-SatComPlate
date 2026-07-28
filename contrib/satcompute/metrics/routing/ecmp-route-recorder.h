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

#ifndef SATCOMPUTE_ECMP_ROUTE_RECORDER_H
#define SATCOMPUTE_ECMP_ROUTE_RECORDER_H

#include "../../routing/satcompute-ipv4-global-routing.h"
#include "../../topology/satellite-topology.h"

#include <vector>

namespace ns3 {

class EcmpRouteRecorder
{
public:
  explicit EcmpRouteRecorder(const SatelliteTopology& topology);

  const std::vector<EcmpRouteDecisionEvent>& GetEvents() const;

private:
  void Record(const EcmpRouteDecisionEvent& event);

  std::vector<EcmpRouteDecisionEvent> m_events;
};

} // namespace ns3

#endif
