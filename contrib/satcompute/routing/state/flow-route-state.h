/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FLOW_ROUTE_STATE_H
#define SATCOMPUTE_FLOW_ROUTE_STATE_H

#include "../common/ecmp-flow-key.h"
#include "../common/ecmp-route-candidate.h"

#include <cstdint>
#include <string>

namespace ns3
{

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
    virtual bool FindAssignment(uint32_t nodeId,
                                const EcmpFlowKey& flowKey,
                                FlowRouteAssignment& assignment) const = 0;
    virtual void RecordAssignment(uint32_t nodeId,
                                  const EcmpFlowKey& flowKey,
                                  const EcmpRouteCandidate& candidate,
                                  uint64_t routeEpoch,
                                  const std::string& selectionReason) = 0;
    virtual void ValidateAssignment(uint32_t nodeId,
                                    const EcmpFlowKey& flowKey,
                                    uint64_t routeEpoch,
                                    const std::string& selectionReason) = 0;
    virtual void ReleaseInvalidAssignment(uint32_t nodeId,
                                          const EcmpFlowKey& flowKey,
                                          uint64_t routeEpoch) = 0;
};

} // namespace ns3

#endif
