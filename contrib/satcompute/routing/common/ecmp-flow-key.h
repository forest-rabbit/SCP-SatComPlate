/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ECMP_FLOW_KEY_H
#define SATCOMPUTE_ECMP_FLOW_KEY_H

#include "ns3/ipv4-address.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace ns3
{

struct EcmpFlowKey
{
    Ipv4Address sourceAddress;
    Ipv4Address destinationAddress;
    uint8_t protocol;
    uint16_t sourcePort;
    uint16_t destinationPort;

    EcmpFlowKey() : protocol(0), sourcePort(0), destinationPort(0)
    {
    }

    bool operator<(const EcmpFlowKey& other) const
    {
        return std::make_tuple(sourceAddress.Get(),
                               destinationAddress.Get(),
                               protocol,
                               sourcePort,
                               destinationPort) < std::make_tuple(other.sourceAddress.Get(),
                                                                  other.destinationAddress.Get(),
                                                                  other.protocol,
                                                                  other.sourcePort,
                                                                  other.destinationPort);
    }
};

inline std::array<uint8_t, 21>
EncodeEcmpFlowKey(uint64_t hashSeed, const EcmpFlowKey& key)
{
    std::array<uint8_t, 21> bytes = {};
    std::size_t offset = 0;
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        bytes[offset++] = static_cast<uint8_t>(hashSeed >> shift);
    }

    uint32_t source = key.sourceAddress.Get();
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        bytes[offset++] = static_cast<uint8_t>(source >> shift);
    }

    uint32_t destination = key.destinationAddress.Get();
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        bytes[offset++] = static_cast<uint8_t>(destination >> shift);
    }

    bytes[offset++] = key.protocol;
    bytes[offset++] = static_cast<uint8_t>(key.sourcePort >> 8);
    bytes[offset++] = static_cast<uint8_t>(key.sourcePort);
    bytes[offset++] = static_cast<uint8_t>(key.destinationPort >> 8);
    bytes[offset++] = static_cast<uint8_t>(key.destinationPort);
    return bytes;
}

} // namespace ns3

#endif
