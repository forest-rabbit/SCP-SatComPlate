/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SIZE_AWARE_LOAD_STATE_H
#define SATCOMPUTE_SIZE_AWARE_LOAD_STATE_H

#include "../common/ecmp-route-candidate.h"
#include "size-aware-load-view.h"

#include <cstdint>
#include <map>

namespace ns3
{

struct SizeAwareLoadChange
{
    uint64_t candidateReservedBefore;
    uint64_t candidateReservedAfter;
    uint64_t totalReservedBefore;
    uint64_t totalReservedAfter;
};

class SizeAwareLoadState : public SizeAwareLoadView
{
  public:
    SizeAwareLoadState();

    SizeAwareLoadChange Reserve(uint32_t nodeId,
                                const EcmpRouteCandidate& candidate,
                                uint64_t bytes);
    SizeAwareLoadChange Release(uint32_t nodeId,
                                const EcmpRouteCandidate& candidate,
                                uint64_t bytes);

    uint64_t GetReservedBytes(uint32_t nodeId, const EcmpRouteCandidate& candidate) const override;
    uint64_t GetTotalReservedBytes() const;
    uint64_t GetPeakReservedBytes() const;
    uint64_t GetPeakCandidateReservedBytes() const;
    void Clear();

  private:
    struct NodeNextHopKey
    {
        uint32_t nodeId;
        Ipv4Address gateway;
        uint32_t outputInterface;

        bool operator<(const NodeNextHopKey& other) const;
    };

    std::map<NodeNextHopKey, uint64_t> m_nextHopReservedBytes;
    uint64_t m_totalReservedBytes;
    uint64_t m_peakReservedBytes;
    uint64_t m_peakCandidateReservedBytes;
};

} // namespace ns3

#endif
