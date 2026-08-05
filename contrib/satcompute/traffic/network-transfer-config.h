/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_CONFIG_H
#define SATCOMPUTE_NETWORK_TRANSFER_CONFIG_H

#include "../routing/common/ecmp-flow-key.h"

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ns3
{

inline constexpr uint16_t NETWORK_TRANSFER_DESTINATION_PORT = 9000;
inline constexpr uint16_t NETWORK_TRANSFER_FIRST_SOURCE_PORT = 10000;

class NetworkTransferConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct NetworkTransfer
{
    uint64_t transferId{};
    uint32_t sourceSatelliteId{};
    uint32_t destinationSatelliteId{};
    uint64_t sizeBytes{};
    int64_t arrivalTimeNs{-1};
    Ipv4Address sourceAddress;
    Ipv4Address destinationAddress;
    uint16_t sourcePort{};
    uint16_t destinationPort{NETWORK_TRANSFER_DESTINATION_PORT};
    uint32_t payloadBytesPerPacket{};
    uint64_t packetCount{};
    uint32_t finalPacketPayloadBytes{};
};

uint32_t GetSizeAwareMaximumPayloadBytes();
uint32_t ResolveNetworkTransferPayloadBytes(const std::string& chunkMode,
                                            uint32_t fixedPayloadBytes,
                                            uint64_t transferSizeBytes);
EcmpFlowKey BuildNetworkTransferFlowKey(const NetworkTransfer& transfer);

} // namespace ns3

#endif
