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

#ifndef SATCOMPUTE_FLOW_ROUTE_STATE_H
#define SATCOMPUTE_FLOW_ROUTE_STATE_H

#include "../common/ecmp-flow-key.h"
#include "../common/ecmp-route-candidate.h"

#include <cstdint>
#include <string>

namespace ns3 {

struct FlowRouteAssignment
{
  EcmpRouteCandidate candidate;
  uint64_t reservedBytes;
  uint64_t latestRouteEpoch;
};

class FlowRouteState
{
public:
  virtual ~FlowRouteState()
  {
  }

  virtual bool IsSenderActive(const EcmpFlowKey& flowKey) const = 0;
  virtual bool FindAssignment(
    uint32_t nodeId,
    const EcmpFlowKey& flowKey,
    FlowRouteAssignment& assignment) const = 0;
  virtual void RecordAssignment(
    uint32_t nodeId,
    const EcmpFlowKey& flowKey,
    const EcmpRouteCandidate& candidate,
    uint64_t routeEpoch,
    const std::string& selectionReason) = 0;
  virtual void ValidateAssignment(
    uint32_t nodeId,
    const EcmpFlowKey& flowKey,
    uint64_t routeEpoch,
    const std::string& selectionReason) = 0;
  virtual void ReleaseInvalidAssignment(
    uint32_t nodeId,
    const EcmpFlowKey& flowKey,
    uint64_t routeEpoch) = 0;
};

} // namespace ns3

#endif
