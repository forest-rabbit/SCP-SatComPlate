/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_LINK_WINDOW_H
#define SATCOMPUTE_LINK_WINDOW_H

#include <cstdint>

namespace ns3
{

/** Integrated observations for one directed-link interval. */
struct LinkWindowTotals
{
    int64_t availableNs{}; ///< Time when the logical link is available.
    int64_t busyNs{}; ///< Serialization time, excluding propagation and interframe gaps.
    int64_t availableBusyNs{}; ///< Serialization overlapping logical availability.
    uint64_t txBytes{}; ///< Full frame bytes whose transmission starts in this interval.
    uint64_t txPackets{}; ///< Frames whose transmission starts in this interval.
    uint64_t dropBytes{}; ///< Device queue drop bytes.
    uint64_t dropPackets{}; ///< Device queue drop packets.
    long double serializedBits{}; ///< Serialized bits apportioned across window boundaries.
    long double capacityBitNs{}; ///< Integral of configured bit/s over nanoseconds.
    long double availableCapacityBitNs{}; ///< Integral of available bit/s over nanoseconds.
    long double reservedBitNs{}; ///< Integral of reserved bit/s over nanoseconds.
    long double queueByteNs{}; ///< Integral of queued bytes over nanoseconds.
    uint64_t maxQueueBytes{}; ///< Maximum queued bytes, including the interval start.
    uint64_t peakReservedBps{}; ///< Maximum reserved rate, including the interval start.

    /** Add another interval, preserving maxima. */
    void Add(const LinkWindowTotals& other);
};

/** Pure event-driven integration; no simulator events or routing side effects. */
class LinkWindow
{
  public:
    /** Construct an initially idle link at simulation time zero. */
    LinkWindow(uint64_t rateBps, bool available);
    /** Integrate the old state through nowNs before changing availability/rate. */
    void SetLink(int64_t nowNs, uint64_t rateBps, bool available);
    /** Observe a physical frame transmission beginning at nowNs. */
    void StartTransmission(int64_t nowNs, uint64_t bytes, int64_t serializationNs);
    /** Observe a queue occupancy transition. */
    void SetQueue(int64_t nowNs, uint64_t bytes);
    /** Observe a rate reservation transition, independently of actual traffic. */
    void SetReserved(int64_t nowNs, uint64_t rateBps);
    /** Observe a device queue drop, not an actual transmission. */
    void Drop(int64_t nowNs, uint64_t bytes);
    /** Close the current interval and preserve the state for the next interval. */
    LinkWindowTotals Take(int64_t nowNs);

  private:
    void Advance(int64_t nowNs);

    int64_t m_lastNs{}; ///< Last integrated event timestamp.
    int64_t m_busyUntilNs{}; ///< End of the currently serializing frame.
    long double m_busyBitsPerNs{}; ///< Frame bits apportioned over its actual ns-3 duration.
    uint64_t m_rateBps{}; ///< Current link rate.
    uint64_t m_queueBytes{}; ///< Current queued bytes.
    uint64_t m_reservedBps{}; ///< Current reserved rate.
    bool m_available{}; ///< Current logical availability.
    LinkWindowTotals m_totals; ///< Current interval integrals and counters.
};

} // namespace ns3

#endif
