/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 实现稳定逐流 HRW 评分、选择和排名，供后续确定性策略复用。

#include "hrw-per-flow-policy.h"

#include "../common/fnv1a64.h"

#include "ns3/abort.h"

#include <algorithm>
#include <cstddef>

namespace ns3
{

std::array<uint8_t, 37>
EncodeEcmpHrwKey(uint64_t hashSeed, const EcmpFlowKey& flowKey, const EcmpRouteCandidate& candidate)
{
    std::array<uint8_t, 37> bytes = {};
    const std::array<uint8_t, 21> flowBytes = EncodeEcmpFlowKey(hashSeed, flowKey);
    std::copy(flowBytes.begin(), flowBytes.end(), bytes.begin());

    std::size_t offset = flowBytes.size();
    auto appendUint32 = [&bytes, &offset](uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            bytes[offset++] = static_cast<uint8_t>(value >> shift);
        }
    };
    appendUint32(candidate.gateway.Get());
    appendUint32(candidate.outputInterface);
    appendUint32(candidate.destination.Get());
    appendUint32(candidate.destinationMask.Get());
    return bytes;
}

uint64_t
ScoreEcmpHrwRoute(uint64_t hashSeed,
                  const EcmpFlowKey& flowKey,
                  const EcmpRouteCandidate& candidate)
{
    return Fnv1a64(EncodeEcmpHrwKey(hashSeed, flowKey, candidate));
}

EcmpHrwSelection
SelectEcmpHrwRoute(uint64_t hashSeed,
                   const EcmpFlowKey& flowKey,
                   const std::vector<EcmpRouteCandidate>& candidates)
{
    NS_ABORT_MSG_IF(candidates.empty(), "HRW 要求非空候选集合");
    EcmpHrwSelection selected = {0, ScoreEcmpHrwRoute(hashSeed, flowKey, candidates[0])};
    for (uint32_t index = 1; index < candidates.size(); ++index)
    {
        const uint64_t score = ScoreEcmpHrwRoute(hashSeed, flowKey, candidates[index]);
        if (score > selected.score ||
            (score == selected.score && candidates[index] < candidates[selected.candidateIndex]))
        {
            selected.candidateIndex = index;
            selected.score = score;
        }
    }
    return selected;
}

std::vector<EcmpHrwRank>
RankEcmpHrwRoutes(uint64_t hashSeed,
                  const EcmpFlowKey& flowKey,
                  const std::vector<EcmpRouteCandidate>& candidates)
{
    std::vector<EcmpHrwRank> ranking;
    ranking.reserve(candidates.size());
    for (uint32_t index = 0; index < candidates.size(); ++index)
    {
        ranking.push_back({index, ScoreEcmpHrwRoute(hashSeed, flowKey, candidates[index])});
    }
    std::stable_sort(ranking.begin(),
                     ranking.end(),
                     [&candidates](const EcmpHrwRank& left, const EcmpHrwRank& right) {
                         return left.score > right.score ||
                                (left.score == right.score && candidates[left.candidateIndex] <
                                                                  candidates[right.candidateIndex]);
                     });
    return ranking;
}

NextHopDecision
HrwPerFlowPolicy::Select(const NextHopSelectionContext& context,
                         const std::vector<EcmpRouteCandidate>& candidates)
{
    const EcmpHrwSelection selected =
        SelectEcmpHrwRoute(context.hashSeed, context.flowKey, candidates);
    return {false, selected.candidateIndex, selected.score, "HRW_PER_FLOW"};
}

} // namespace ns3
