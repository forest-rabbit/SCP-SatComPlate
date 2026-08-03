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

// 在 ns-3 ECMP 最短路图内选择剩余瓶颈容量最大的完整路径。

#include "capacity-aware-hrw-policy.h"

#include "hrw-per-flow-policy.h"

#include "ns3/abort.h"

#include <algorithm>
#include <limits>

namespace ns3 {

CapacityAwareHrwPolicy::CapacityAwareHrwPolicy(
  const CapacityAwarePathView& pathView,
  const CapacityReservationState& reservationState)
  : m_pathView(&pathView),
    m_reservationState(&reservationState)
{
}

CapacityAwareHrwPolicy::PathSearchResult
CapacityAwareHrwPolicy::FindBestSuffix(
  const PathSelectionContext& context,
  uint32_t currentSatelliteId,
  std::map<uint32_t, PathSearchResult>& memo,
  std::set<uint32_t>& visiting) const
{
  if (currentSatelliteId == context.destinationSatelliteId)
    {
      PathSearchResult destination;
      destination.found = true;
      destination.bottleneckRateBps =
        std::numeric_limits<uint64_t>::max();
      return destination;
    }

  auto cached = memo.find(currentSatelliteId);
  if (cached != memo.end())
    {
      return cached->second;
    }
  if (!visiting.insert(currentSatelliteId).second)
    {
      return PathSearchResult();
    }

  std::vector<EcmpRouteCandidate> candidates =
    m_pathView->GetEcmpRouteCandidates(
      currentSatelliteId,
      context.destinationSatelliteId);
  PathSearchResult best;
  if (!candidates.empty())
    {
      std::vector<EcmpHrwRank> ranking =
        RankEcmpHrwRoutes(context.hashSeed,
                          context.flowKey,
                          candidates);
      for (const auto& rank : ranking)
        {
          const EcmpRouteCandidate& candidate =
            candidates[rank.candidateIndex];
          uint32_t nextSatelliteId =
            m_pathView->GetNextHopSatelliteId(
              currentSatelliteId,
              candidate.outputInterface);
          CapacityAwarePathHop hop = {
            currentSatelliteId,
            nextSatelliteId,
            candidate,
            m_pathView->GetIslDataRateBps(
              currentSatelliteId,
              candidate.outputInterface)
          };
          uint64_t residualRate =
            m_reservationState->GetResidualRateBps(hop);
          if (residualRate == 0)
            {
              continue;
            }

          PathSearchResult suffix =
            FindBestSuffix(context,
                           nextSatelliteId,
                           memo,
                           visiting);
          if (!suffix.found)
            {
              continue;
            }
          uint64_t pathRate =
            std::min(residualRate, suffix.bottleneckRateBps);
          if (!best.found || pathRate > best.bottleneckRateBps)
            {
              best = suffix;
              best.found = true;
              best.bottleneckRateBps = pathRate;
              best.hops.insert(best.hops.begin(), hop);
            }
        }
    }

  visiting.erase(currentSatelliteId);
  memo.insert(std::make_pair(currentSatelliteId, best));
  return best;
}

bool
CapacityAwareHrwPolicy::FindPath(
  const PathSelectionContext& context,
  CapacityAwarePath& path) const
{
  NS_ABORT_MSG_IF(
    context.sourceSatelliteId == context.destinationSatelliteId,
    "capacity-aware path 要求不同源和目的卫星");
  std::map<uint32_t, PathSearchResult> memo;
  std::set<uint32_t> visiting;
  PathSearchResult result =
    FindBestSuffix(context,
                   context.sourceSatelliteId,
                   memo,
                   visiting);
  if (!result.found)
    {
      path = CapacityAwarePath();
      return false;
    }
  NS_ABORT_MSG_IF(result.hops.empty()
                    || result.bottleneckRateBps == 0
                    || result.bottleneckRateBps
                         == std::numeric_limits<uint64_t>::max(),
                  "capacity-aware path 结果无效");
  path.hops = result.hops;
  path.admittedRateBps = result.bottleneckRateBps;
  return true;
}

} // namespace ns3
