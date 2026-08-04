/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 维护 size-aware 策略独有的节点下一跳声明字节预留账本。

#include "size-aware-load-state.h"

#include "ns3/abort.h"

#include <limits>
#include <tuple>

namespace ns3
{

SizeAwareLoadState::SizeAwareLoadState()
    : m_totalReservedBytes(0),
      m_peakReservedBytes(0),
      m_peakCandidateReservedBytes(0)
{
}

bool
SizeAwareLoadState::NodeNextHopKey::operator<(const NodeNextHopKey& other) const
{
    return std::make_tuple(nodeId, gateway.Get(), outputInterface) <
           std::make_tuple(other.nodeId, other.gateway.Get(), other.outputInterface);
}

SizeAwareLoadChange
SizeAwareLoadState::Reserve(uint32_t nodeId, const EcmpRouteCandidate& candidate, uint64_t bytes)
{
    NS_ABORT_MSG_IF(bytes == 0, "size-aware load reservation 要求正字节数");
    NodeNextHopKey key = {nodeId, candidate.gateway, candidate.outputInterface};
    uint64_t& candidateReserved = m_nextHopReservedBytes[key];
    SizeAwareLoadChange change = {candidateReserved, 0, m_totalReservedBytes, 0};
    NS_ABORT_MSG_IF(candidateReserved > std::numeric_limits<uint64_t>::max() - bytes,
                    "size-aware candidate reserved bytes 溢出");
    NS_ABORT_MSG_IF(m_totalReservedBytes > std::numeric_limits<uint64_t>::max() - bytes,
                    "size-aware total reserved bytes 溢出");
    candidateReserved += bytes;
    m_totalReservedBytes += bytes;
    if (m_totalReservedBytes > m_peakReservedBytes)
    {
        m_peakReservedBytes = m_totalReservedBytes;
    }
    if (candidateReserved > m_peakCandidateReservedBytes)
    {
        m_peakCandidateReservedBytes = candidateReserved;
    }
    change.candidateReservedAfter = candidateReserved;
    change.totalReservedAfter = m_totalReservedBytes;
    return change;
}

SizeAwareLoadChange
SizeAwareLoadState::Release(uint32_t nodeId, const EcmpRouteCandidate& candidate, uint64_t bytes)
{
    NS_ABORT_MSG_IF(bytes == 0, "size-aware load release 要求正字节数");
    NodeNextHopKey key = {nodeId, candidate.gateway, candidate.outputInterface};
    auto reserved = m_nextHopReservedBytes.find(key);
    NS_ABORT_MSG_IF(reserved == m_nextHopReservedBytes.end() || reserved->second < bytes ||
                        m_totalReservedBytes < bytes,
                    "size-aware reserved bytes 状态不一致");
    SizeAwareLoadChange change = {reserved->second,
                                  reserved->second - bytes,
                                  m_totalReservedBytes,
                                  m_totalReservedBytes - bytes};
    reserved->second -= bytes;
    m_totalReservedBytes -= bytes;
    if (reserved->second == 0)
    {
        m_nextHopReservedBytes.erase(reserved);
    }
    return change;
}

uint64_t
SizeAwareLoadState::GetReservedBytes(uint32_t nodeId, const EcmpRouteCandidate& candidate) const
{
    NodeNextHopKey key = {nodeId, candidate.gateway, candidate.outputInterface};
    auto found = m_nextHopReservedBytes.find(key);
    return found == m_nextHopReservedBytes.end() ? 0 : found->second;
}

uint64_t
SizeAwareLoadState::GetTotalReservedBytes() const
{
    return m_totalReservedBytes;
}

uint64_t
SizeAwareLoadState::GetPeakReservedBytes() const
{
    return m_peakReservedBytes;
}

uint64_t
SizeAwareLoadState::GetPeakCandidateReservedBytes() const
{
    return m_peakCandidateReservedBytes;
}

void
SizeAwareLoadState::Clear()
{
    m_nextHopReservedBytes.clear();
    m_totalReservedBytes = 0;
    m_peakReservedBytes = 0;
    m_peakCandidateReservedBytes = 0;
}

} // namespace ns3
