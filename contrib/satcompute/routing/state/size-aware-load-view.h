/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SIZE_AWARE_LOAD_VIEW_H
#define SATCOMPUTE_SIZE_AWARE_LOAD_VIEW_H

#include "../common/ecmp-route-candidate.h"

#include <cstdint>

namespace ns3
{

class SizeAwareLoadView
{
  public:
    virtual ~SizeAwareLoadView()
    {
    }

    virtual uint64_t GetReservedBytes(uint32_t nodeId,
                                      const EcmpRouteCandidate& candidate) const = 0;
};

} // namespace ns3

#endif
