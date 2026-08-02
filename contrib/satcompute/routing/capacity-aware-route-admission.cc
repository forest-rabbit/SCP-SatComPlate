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

// 在等价最短路径图中选择剩余带宽最大的路径并维护活动流速率预留。

#include "capacity-aware-route-admission.h"

#include "../topology/satellite-topology.h"

#include "ns3/abort.h"

#include <algorithm>
#include <limits>

namespace ns3 {

CapacityAwareRouteAdmission::CapacityAwareRouteAdmission(
  const SatelliteTopology& topology)
  : m_topology(&topology)
{
  NS_ABORT_MSG_IF(!topology.IsCapacityAwareRouting(),
                  "capacity-aware admission 要求对应 routingMode");
}

uint64_t
CapacityAwareRouteAdmission::GetResidualRateBps(
  const CapacityAwarePathHop& hop) const
{
  DirectedLinkKey key =
    std::make_pair(hop.sourceSatelliteId, hop.candidate.outputInterface);
  auto reserved = m_reservedRateBps.find(key);
  uint64_t reservedRate =
    reserved == m_reservedRateBps.end() ? 0 : reserved->second;
  NS_ABORT_MSG_IF(reservedRate > hop.linkRateBps,
                  "capacity-aware 已预留速率超过 ISL 带宽");
  return hop.linkRateBps - reservedRate;
}

CapacityAwareRouteAdmission::PathSearchResult
CapacityAwareRouteAdmission::FindBestSuffix(
  const EcmpFlowKey& flowKey,
  uint32_t currentSatelliteId,
  uint32_t destinationSatelliteId,
  std::map<uint32_t, PathSearchResult>& memo,
  std::set<uint32_t>& visiting) const
{
  if (currentSatelliteId == destinationSatelliteId)
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
    m_topology->GetEcmpRouteCandidates(currentSatelliteId,
                                       destinationSatelliteId);
  PathSearchResult best;
  if (!candidates.empty())
    {
      std::vector<EcmpHrwRank> ranking =
        RankEcmpHrwRoutes(m_topology->GetEcmpHashSeed(),
                          flowKey,
                          candidates);
      for (const auto& rank : ranking)
        {
          const EcmpRouteCandidate& candidate =
            candidates[rank.candidateIndex];
          uint32_t nextSatelliteId =
            m_topology->GetNextHopSatelliteId(
              currentSatelliteId,
              candidate.outputInterface);
          CapacityAwarePathHop hop = {
            currentSatelliteId,
            nextSatelliteId,
            candidate,
            m_topology->GetIslDataRateBps(
              currentSatelliteId,
              candidate.outputInterface)
          };
          uint64_t residualRate = GetResidualRateBps(hop);
          if (residualRate == 0)
            {
              continue;
            }

          PathSearchResult suffix =
            FindBestSuffix(flowKey,
                           nextSatelliteId,
                           destinationSatelliteId,
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
CapacityAwareRouteAdmission::FindAvailablePath(
  const EcmpFlowKey& flowKey,
  uint32_t sourceSatelliteId,
  uint32_t destinationSatelliteId,
  CapacityAwarePath& path) const
{
  NS_ABORT_MSG_IF(sourceSatelliteId == destinationSatelliteId,
                  "capacity-aware path 要求不同源和目的卫星");
  std::map<uint32_t, PathSearchResult> memo;
  std::set<uint32_t> visiting;
  PathSearchResult result =
    FindBestSuffix(flowKey,
                   sourceSatelliteId,
                   destinationSatelliteId,
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

void
CapacityAwareRouteAdmission::Reserve(uint64_t transferId,
                                     const CapacityAwarePath& path)
{
  NS_ABORT_MSG_IF(transferId == 0
                    || path.hops.empty()
                    || path.admittedRateBps == 0,
                  "capacity-aware reservation 参数无效");
  NS_ABORT_MSG_IF(m_activePaths.find(transferId) != m_activePaths.end(),
                  "capacity-aware transfer 重复预留: " << transferId);
  for (const auto& hop : path.hops)
    {
      NS_ABORT_MSG_IF(GetResidualRateBps(hop) < path.admittedRateBps,
                      "capacity-aware path 在提交前失去可用容量");
    }
  for (const auto& hop : path.hops)
    {
      DirectedLinkKey key =
        std::make_pair(hop.sourceSatelliteId,
                       hop.candidate.outputInterface);
      uint64_t& reservedRate = m_reservedRateBps[key];
      NS_ABORT_MSG_IF(
        reservedRate > std::numeric_limits<uint64_t>::max()
                         - path.admittedRateBps,
        "capacity-aware reserved rate 溢出");
      reservedRate += path.admittedRateBps;
    }
  m_activePaths.insert(std::make_pair(transferId, path));
}

void
CapacityAwareRouteAdmission::Release(uint64_t transferId)
{
  auto active = m_activePaths.find(transferId);
  NS_ABORT_MSG_IF(active == m_activePaths.end(),
                  "capacity-aware transfer 不存在活动预留: " << transferId);
  for (const auto& hop : active->second.hops)
    {
      DirectedLinkKey key =
        std::make_pair(hop.sourceSatelliteId,
                       hop.candidate.outputInterface);
      auto reserved = m_reservedRateBps.find(key);
      NS_ABORT_MSG_IF(reserved == m_reservedRateBps.end()
                        || reserved->second
                             < active->second.admittedRateBps,
                      "capacity-aware release 状态不一致");
      reserved->second -= active->second.admittedRateBps;
      if (reserved->second == 0)
        {
          m_reservedRateBps.erase(reserved);
        }
    }
  m_activePaths.erase(active);
}

} // namespace ns3
