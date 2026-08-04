/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CAPACITY_AWARE_PATH_TYPES_H
#define SATCOMPUTE_CAPACITY_AWARE_PATH_TYPES_H

#include "../common/ecmp-route-candidate.h"

#include <cstdint>
#include <vector>

namespace ns3
{

struct CapacityAwarePathHop
{
    uint32_t sourceSatelliteId;
    uint32_t destinationSatelliteId;
    EcmpRouteCandidate candidate;
    uint64_t linkRateBps;
};

struct CapacityAwarePath
{
    std::vector<CapacityAwarePathHop> hops;
    uint64_t admittedRateBps;

    CapacityAwarePath()
        : admittedRateBps(0)
    {
    }
};

} // namespace ns3

#endif
