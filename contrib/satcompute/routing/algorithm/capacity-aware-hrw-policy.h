/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CAPACITY_AWARE_HRW_POLICY_H
#define SATCOMPUTE_CAPACITY_AWARE_HRW_POLICY_H

#include "capacity-aware-path-view.h"
#include "path-policy.h"
#include "../state/capacity-reservation-state.h"

#include <map>
#include <set>

namespace ns3
{

class CapacityAwareHrwPolicy : public PathPolicy
{
  public:
    CapacityAwareHrwPolicy(const CapacityAwarePathView& pathView,
                           const CapacityReservationState& reservationState);

    bool FindPath(const PathSelectionContext& context, CapacityAwarePath& path) const override;

  private:
    struct PathSearchResult
    {
        bool found;
        uint64_t bottleneckRateBps;
        std::vector<CapacityAwarePathHop> hops;

        PathSearchResult()
            : found(false),
              bottleneckRateBps(0)
        {
        }
    };

    PathSearchResult FindBestSuffix(const PathSelectionContext& context,
                                    uint32_t currentSatelliteId,
                                    std::map<uint32_t, PathSearchResult>& memo,
                                    std::set<uint32_t>& visiting) const;

    const CapacityAwarePathView* m_pathView;
    const CapacityReservationState* m_reservationState;
};

} // namespace ns3

#endif
