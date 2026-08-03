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

// 使用既有 FNV-1a-64 五元组编码执行确定性的逐流取模选择。

#include "hash-per-flow-policy.h"

#include "../common/fnv1a64.h"

#include "ns3/abort.h"

namespace ns3 {

NextHopDecision
HashPerFlowPolicy::Select(
  const NextHopSelectionContext& context,
  const std::vector<EcmpRouteCandidate>& candidates)
{
  NS_ABORT_MSG_IF(candidates.empty(),
                  "hash-per-flow 要求非空候选集合");
  uint64_t hashValue =
    Fnv1a64(EncodeEcmpFlowKey(context.hashSeed, context.flowKey));
  return {
    false,
    static_cast<uint32_t>(hashValue % candidates.size()),
    hashValue,
    "HASH_PER_FLOW"
  };
}

} // namespace ns3
