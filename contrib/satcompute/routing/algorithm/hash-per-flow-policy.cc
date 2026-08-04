/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 使用既有 FNV-1a-64 五元组编码执行确定性的逐流取模选择。

#include "hash-per-flow-policy.h"

#include "../common/fnv1a64.h"

#include "ns3/abort.h"

namespace ns3
{

NextHopDecision
HashPerFlowPolicy::Select(const NextHopSelectionContext& context,
                          const std::vector<EcmpRouteCandidate>& candidates)
{
    NS_ABORT_MSG_IF(candidates.empty(), "hash-per-flow 要求非空候选集合");
    uint64_t hashValue = Fnv1a64(EncodeEcmpFlowKey(context.hashSeed, context.flowKey));
    return {false,
            static_cast<uint32_t>(hashValue % candidates.size()),
            hashValue,
            "HASH_PER_FLOW"};
}

} // namespace ns3
