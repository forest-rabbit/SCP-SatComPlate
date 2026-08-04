/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FNV1A64_H
#define SATCOMPUTE_FNV1A64_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace ns3
{

template <std::size_t N>
inline uint64_t
Fnv1a64(const std::array<uint8_t, N>& bytes)
{
    static const uint64_t offsetBasis = 14695981039346656037ULL;
    static const uint64_t prime = 1099511628211ULL;

    uint64_t hash = offsetBasis;
    for (uint8_t byte : bytes)
    {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

} // namespace ns3

#endif
