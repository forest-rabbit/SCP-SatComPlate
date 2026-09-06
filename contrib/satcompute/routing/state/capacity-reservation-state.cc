/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 维护 capacity-aware 活动完整路径和有向链路速率预留账本。

#include "capacity-reservation-state.h"

#include "ns3/abort.h"

#include <algorithm>
#include <limits>

namespace ns3
{

void
CapacityReservationState::SetObserver(Callback<void, uint32_t, uint32_t, uint64_t> observer)
{
    m_observer = observer;
    if (!m_observer.IsNull())
    {
        for (const auto& [key, rate] : m_reservedRateBps)
        {
            m_observer(key.first, key.second, rate);
        }
    }
}

uint64_t
CapacityReservationState::GetResidualRateBps(const CapacityAwarePathHop& hop) const
{
    DirectedLinkKey key = std::make_pair(hop.sourceSatelliteId, hop.candidate.outputInterface);
    auto reserved = m_reservedRateBps.find(key);
    uint64_t reservedRate = reserved == m_reservedRateBps.end() ? 0 : reserved->second;
    NS_ABORT_MSG_IF(reservedRate > hop.linkRateBps, "capacity-aware 已预留速率超过 ISL 带宽");
    return hop.linkRateBps - reservedRate;
}

bool
CapacityReservationState::HasActivePath(uint64_t transferId) const
{
    return m_activePaths.find(transferId) != m_activePaths.end();
}

bool
CapacityReservationState::IsActivePathValid(uint64_t transferId,
                                            uint32_t destinationSatelliteId,
                                            const CapacityAwarePathView& pathView) const
{
    auto active = m_activePaths.find(transferId);
    NS_ABORT_MSG_IF(active == m_activePaths.end(),
                    "capacity-aware 无法检查不存在的活动路径: " << transferId);

    uint32_t expectedSource = active->second.hops.front().sourceSatelliteId;
    for (const auto& hop : active->second.hops)
    {
        if (hop.sourceSatelliteId != expectedSource)
        {
            return false;
        }
        std::vector<EcmpRouteCandidate> candidates =
            pathView.GetEcmpRouteCandidates(hop.sourceSatelliteId, destinationSatelliteId);
        if (std::find(candidates.begin(), candidates.end(), hop.candidate) == candidates.end())
        {
            return false;
        }

        uint64_t currentRateBps =
            pathView.GetIslDataRateBps(hop.sourceSatelliteId, hop.candidate.outputInterface);
        DirectedLinkKey key = std::make_pair(hop.sourceSatelliteId, hop.candidate.outputInterface);
        auto reserved = m_reservedRateBps.find(key);
        if (reserved == m_reservedRateBps.end() || reserved->second > currentRateBps)
        {
            return false;
        }
        expectedSource = hop.destinationSatelliteId;
    }
    return expectedSource == destinationSatelliteId;
}

CapacityAwareRuntimeSummary
CapacityReservationState::CollectSummary() const
{
    CapacityAwareRuntimeSummary summary;
    summary.activePathCountAtEnd = m_activePaths.size();
    summary.reservedDirectedLinkCountAtEnd = m_reservedRateBps.size();
    for (const auto& reservation : m_reservedRateBps)
    {
        NS_ABORT_MSG_IF(summary.totalReservedRateBpsAtEnd >
                            std::numeric_limits<uint64_t>::max() - reservation.second,
                        "capacity-aware total reserved rate 溢出");
        summary.totalReservedRateBpsAtEnd += reservation.second;
    }
    return summary;
}

void
CapacityReservationState::Reserve(uint64_t transferId, const CapacityAwarePath& path)
{
    NS_ABORT_MSG_IF(transferId == 0 || path.hops.empty() || path.admittedRateBps == 0,
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
        DirectedLinkKey key = std::make_pair(hop.sourceSatelliteId, hop.candidate.outputInterface);
        uint64_t& reservedRate = m_reservedRateBps[key];
        NS_ABORT_MSG_IF(reservedRate > std::numeric_limits<uint64_t>::max() - path.admittedRateBps,
                        "capacity-aware reserved rate 溢出");
        reservedRate += path.admittedRateBps;
        if (!m_observer.IsNull())
        {
            m_observer(key.first, key.second, reservedRate);
        }
    }
    m_activePaths.insert(std::make_pair(transferId, path));
}

void
CapacityReservationState::Release(uint64_t transferId)
{
    auto active = m_activePaths.find(transferId);
    NS_ABORT_MSG_IF(active == m_activePaths.end(),
                    "capacity-aware transfer 不存在活动预留: " << transferId);
    for (const auto& hop : active->second.hops)
    {
        DirectedLinkKey key = std::make_pair(hop.sourceSatelliteId, hop.candidate.outputInterface);
        auto reserved = m_reservedRateBps.find(key);
        NS_ABORT_MSG_IF(reserved == m_reservedRateBps.end() ||
                            reserved->second < active->second.admittedRateBps,
                        "capacity-aware release 状态不一致");
        reserved->second -= active->second.admittedRateBps;
        if (!m_observer.IsNull())
        {
            m_observer(key.first, key.second, reserved->second);
        }
        if (reserved->second == 0)
        {
            m_reservedRateBps.erase(reserved);
        }
    }
    m_activePaths.erase(active);
}

} // namespace ns3
