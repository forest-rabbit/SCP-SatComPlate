/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_PATH_POLICY_H
#define SATCOMPUTE_PATH_POLICY_H

#include "capacity-aware-path-types.h"
#include "../common/ecmp-flow-key.h"

#include <cstdint>

namespace ns3
{

struct PathSelectionContext
{
    EcmpFlowKey flowKey;
    uint32_t sourceSatelliteId;
    uint32_t destinationSatelliteId;
    uint64_t hashSeed;
};

class PathPolicy
{
  public:
    virtual ~PathPolicy()
    {
    }

    virtual bool FindPath(const PathSelectionContext& context, CapacityAwarePath& path) const = 0;
};

} // namespace ns3

#endif
