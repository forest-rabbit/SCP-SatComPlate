/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "link-window.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3
{

void
LinkWindowTotals::Add(const LinkWindowTotals& other)
{
    availableNs += other.availableNs;
    busyNs += other.busyNs;
    availableBusyNs += other.availableBusyNs;
    txBytes += other.txBytes;
    txPackets += other.txPackets;
    dropBytes += other.dropBytes;
    dropPackets += other.dropPackets;
    serializedBits += other.serializedBits;
    capacityBitNs += other.capacityBitNs;
    availableCapacityBitNs += other.availableCapacityBitNs;
    reservedBitNs += other.reservedBitNs;
    queueByteNs += other.queueByteNs;
    maxQueueBytes = std::max(maxQueueBytes, other.maxQueueBytes);
    peakReservedBps = std::max(peakReservedBps, other.peakReservedBps);
}

LinkWindow::LinkWindow(uint64_t rateBps, bool available)
    : m_rateBps(rateBps), m_available(available)
{
    if (rateBps == 0)
    {
        throw std::invalid_argument("link metric rate must be positive");
    }
}

void
LinkWindow::Advance(int64_t nowNs)
{
    if (nowNs < m_lastNs)
    {
        throw std::invalid_argument("link metric time must not move backwards");
    }
    const int64_t elapsed = nowNs - m_lastNs;
    const int64_t busy = std::max<int64_t>(0, std::min(nowNs, m_busyUntilNs) - m_lastNs);
    m_totals.busyNs += busy;
    m_totals.serializedBits += busy * m_busyBitsPerNs;
    m_totals.capacityBitNs += static_cast<long double>(m_rateBps) * elapsed;
    if (m_available)
    {
        m_totals.availableNs += elapsed;
        m_totals.availableBusyNs += busy;
        m_totals.availableCapacityBitNs += static_cast<long double>(m_rateBps) * elapsed;
    }
    m_totals.reservedBitNs += static_cast<long double>(m_reservedBps) * elapsed;
    m_totals.queueByteNs += static_cast<long double>(m_queueBytes) * elapsed;
    m_lastNs = nowNs;
}

void
LinkWindow::SetLink(int64_t nowNs, uint64_t rateBps, bool available)
{
    if (rateBps == 0)
    {
        throw std::invalid_argument("link metric rate must be positive");
    }
    Advance(nowNs);
    m_rateBps = rateBps;
    m_available = available;
}

void
LinkWindow::StartTransmission(int64_t nowNs, uint64_t bytes, int64_t serializationNs)
{
    if (serializationNs <= 0 || nowNs < m_busyUntilNs ||
        nowNs > std::numeric_limits<int64_t>::max() - serializationNs)
    {
        throw std::invalid_argument("invalid or overlapping link transmission");
    }
    Advance(nowNs);
    m_busyUntilNs = nowNs + serializationNs;
    m_busyBitsPerNs = static_cast<long double>(bytes) * 8 / serializationNs;
    m_totals.txBytes += bytes;
    ++m_totals.txPackets;
}

void
LinkWindow::SetQueue(int64_t nowNs, uint64_t bytes)
{
    Advance(nowNs);
    m_queueBytes = bytes;
    m_totals.maxQueueBytes = std::max(m_totals.maxQueueBytes, bytes);
}

void
LinkWindow::SetReserved(int64_t nowNs, uint64_t rateBps)
{
    Advance(nowNs);
    m_reservedBps = rateBps;
    m_totals.peakReservedBps = std::max(m_totals.peakReservedBps, rateBps);
}

void
LinkWindow::Drop(int64_t nowNs, uint64_t bytes)
{
    Advance(nowNs);
    m_totals.dropBytes += bytes;
    ++m_totals.dropPackets;
}

LinkWindowTotals
LinkWindow::Take(int64_t nowNs)
{
    Advance(nowNs);
    const LinkWindowTotals result = m_totals;
    m_totals = {};
    m_totals.maxQueueBytes = m_queueBytes;
    m_totals.peakReservedBps = m_reservedBps;
    return result;
}

} // namespace ns3
