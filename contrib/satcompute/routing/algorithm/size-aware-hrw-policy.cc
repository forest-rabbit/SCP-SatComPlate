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

// 在 HRW 前两名之间按声明字节预留选择，并维护节点级 sticky assignment。

#include "size-aware-hrw-policy.h"

#include "hrw-per-flow-policy.h"

#include "ns3/abort.h"

#include <algorithm>

namespace ns3 {

SizeAwareHrwPolicy::SizeAwareHrwPolicy(FlowRouteState& flowState,
                                       const SizeAwareLoadView& loadView)
  : m_flowState(&flowState),
    m_loadView(&loadView)
{
}

NextHopDecision
SizeAwareHrwPolicy::Select(
  const NextHopSelectionContext& context,
  const std::vector<EcmpRouteCandidate>& candidates)
{
  NS_ABORT_MSG_IF(candidates.empty()
                    || m_flowState == nullptr
                    || m_loadView == nullptr
                    || !m_flowState->IsSenderActive(context.flowKey),
                  "size-aware HRW 要求活动 flow 和非空候选");

  FlowRouteAssignment sticky;
  if (m_flowState->FindAssignment(context.nodeId,
                                  context.flowKey,
                                  sticky))
    {
      auto selected =
        std::find(candidates.begin(), candidates.end(), sticky.candidate);
      if (selected != candidates.end())
        {
          uint32_t selectedIndex =
            static_cast<uint32_t>(selected - candidates.begin());
          m_flowState->ValidateAssignment(context.nodeId,
                                          context.flowKey,
                                          context.routeEpoch,
                                          "SIZE_AWARE_STICKY");
          return {
            false,
            selectedIndex,
            ScoreEcmpHrwRoute(context.hashSeed,
                              context.flowKey,
                              candidates[selectedIndex]),
            "SIZE_AWARE_STICKY"
          };
        }
      m_flowState->ReleaseInvalidAssignment(context.nodeId,
                                            context.flowKey,
                                            context.routeEpoch);
    }

  std::vector<EcmpHrwRank> ranking =
    RankEcmpHrwRoutes(context.hashSeed, context.flowKey, candidates);
  EcmpHrwRank selected = ranking[0];
  std::string selectionReason = "SIZE_AWARE_HRW_PRIMARY";
  if (ranking.size() >= 2)
    {
      uint64_t primaryLoad =
        m_loadView->GetReservedBytes(
          context.nodeId,
          candidates[ranking[0].candidateIndex]);
      uint64_t secondaryLoad =
        m_loadView->GetReservedBytes(
          context.nodeId,
          candidates[ranking[1].candidateIndex]);
      if (secondaryLoad < primaryLoad)
        {
          selected = ranking[1];
          selectionReason = "SIZE_AWARE_HRW_SECONDARY";
        }
    }

  m_flowState->RecordAssignment(context.nodeId,
                                context.flowKey,
                                candidates[selected.candidateIndex],
                                context.routeEpoch,
                                selectionReason);
  return {
    false,
    selected.candidateIndex,
    selected.score,
    selectionReason
  };
}

} // namespace ns3
