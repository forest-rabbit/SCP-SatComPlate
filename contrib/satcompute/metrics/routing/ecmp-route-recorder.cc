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

// 订阅逐流 ECMP 选路事件，为指标输出保存可审计的路由证据。

#include "ecmp-route-recorder.h"

#include "../../routing/ns3/satcompute-ipv4-global-routing-helper.h"

#include "ns3/abort.h"
#include "ns3/callback.h"

namespace ns3 {

EcmpRouteRecorder::EcmpRouteRecorder(const SatelliteTopology& topology)
{
  for (uint32_t index = 0; index < topology.GetNodeCount(); ++index)
    {
      bool connected =
        SatComputeIpv4GlobalRoutingHelper::GetRouting(topology.GetNode(index))
          ->TraceConnectWithoutContext(
            "EcmpRouteDecision",
            MakeCallback(&EcmpRouteRecorder::Record, this));
      NS_ABORT_MSG_IF(!connected,
                      "无法连接 EcmpRouteDecision trace source，sat_id="
                        << topology.GetSatelliteIdByNodeIndex(index));
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
