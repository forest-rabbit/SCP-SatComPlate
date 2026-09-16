/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_TRANSFER_KEY_H
#define SATCOMPUTE_PROTECTION_TRANSFER_KEY_H
#include "../common/protection-types.h"
#include <compare>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
/** Canonical order within one request nanosecond; never use event UID or pointer order. */
struct ProtectionTransferKey
{
    uint64_t taskId{};             ///< Stable logical task identity.
    uint64_t attemptGeneration{};  ///< Owning attempt, zero in G2.
    ProtectionTransferKind kind{}; ///< Explicit enum order is part of the ID contract.
    uint64_t sequence{};           ///< Covered WU boundary; zero for initialization.
    /** Lexicographic canonical order, independent of event insertion order. */
    auto operator<=>(const ProtectionTransferKey&) const = default;
};

/** Monotonic disjoint range above ALL pre-registered ordinary transfer IDs. */
class ProtectionTransferIds
{
  public:
    /** @param maximumOrdinaryId Largest pre-registered ordinary transfer ID. */
    explicit ProtectionTransferIds(uint64_t maximumOrdinaryId) : m_last(maximumOrdinaryId)
    {
    }

    /** @return Next positive ID; throws instead of wrapping when exhausted. */
    uint64_t Next()
    {
        if (m_last == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("protection transfer ID range exhausted");
        return ++m_last;
    }

  private:
    uint64_t m_last; ///< Last issued/reserved ID, never recycled.
};
} // namespace ns3::protection
#endif
