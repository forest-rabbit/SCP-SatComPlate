/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CAPACITY_AWARE_PATH_VIEW_H
#define SATCOMPUTE_CAPACITY_AWARE_PATH_VIEW_H

#include "../common/ecmp-route-candidate.h"

#include <cstdint>
#include <vector>

namespace ns3
{

class CapacityAwarePathView
{
  public:
    virtual ~CapacityAwarePathView()
    {
    }

    virtual std::vector<EcmpRouteCandidate> GetEcmpRouteCandidates(
        uint32_t sourceSatelliteId,
        uint32_t destinationSatelliteId) const = 0;
    virtual uint32_t GetNextHopSatelliteId(uint32_t sourceSatelliteId,
                                           uint32_t outputInterface) const = 0;
    virtual uint64_t GetIslDataRateBps(uint32_t sourceSatelliteId,
                                       uint32_t outputInterface) const = 0;
};

} // namespace ns3

#endif
