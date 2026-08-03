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

#ifndef SATCOMPUTE_CAPACITY_AWARE_HRW_POLICY_H
#define SATCOMPUTE_CAPACITY_AWARE_HRW_POLICY_H

#include "capacity-aware-path-view.h"
#include "path-policy.h"
#include "../state/capacity-reservation-state.h"

#include <map>
#include <set>

namespace ns3 {

class CapacityAwareHrwPolicy : public PathPolicy
{
public:
  CapacityAwareHrwPolicy(
    const CapacityAwarePathView& pathView,
    const CapacityReservationState& reservationState);

  bool FindPath(const PathSelectionContext& context,
                CapacityAwarePath& path) const override;

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

  PathSearchResult FindBestSuffix(
    const PathSelectionContext& context,
    uint32_t currentSatelliteId,
    std::map<uint32_t, PathSearchResult>& memo,
    std::set<uint32_t>& visiting) const;

  const CapacityAwarePathView* m_pathView;
  const CapacityReservationState* m_reservationState;
};

} // namespace ns3

#endif
