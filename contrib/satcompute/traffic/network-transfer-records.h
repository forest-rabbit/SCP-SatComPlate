/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_RECORDS_H
#define SATCOMPUTE_NETWORK_TRANSFER_RECORDS_H

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <string>

namespace ns3
{

struct ApplicationMetrics
{
    uint64_t sinkApplications{};
    uint64_t sentBytes{};
    uint64_t receivedBytes{};
};

struct TransferFlowMetadata
{
    uint64_t transferId;
    Ipv4Address sourceAddress;
    Ipv4Address destinationAddress;
    uint8_t protocol;
    uint16_t sourcePort;
    uint16_t destinationPort;
    uint64_t plannedApplicationPayloadBytes;
    uint64_t receivedApplicationPayloadBytes;
};

struct TransferSummaryRecord
{
    uint64_t transferId;
    uint32_t sourceSatelliteId;
    uint32_t destinationSatelliteId;
    Ipv4Address sourceAddress;
    Ipv4Address destinationAddress;
    uint16_t sourcePort;
    uint16_t destinationPort;
    uint64_t declaredSizeBytes;
    uint32_t payloadBytesPerPacket;
    std::string pacingMode;
    uint64_t derivedPacketCount;
    uint32_t finalPacketPayloadBytes;
    int64_t arrivalTimeNs;
    int64_t lastSendTimeNs;
    uint64_t sentApplicationBytes;
    uint64_t receivedApplicationBytes;
    uint64_t receivedPacketCount;
    int64_t completionTimeNs;
    int64_t completionDelayNs;
    std::string transferState;
    uint64_t sentPacketCount;
};

} // namespace ns3

#endif
