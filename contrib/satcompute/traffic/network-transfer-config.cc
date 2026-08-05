/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Derive deterministic UDP plans for task input and result transfers.

#include "network-transfer-config.h"

namespace ns3
{

namespace
{

constexpr uint64_t SMALL_TRANSFER_MAX_BYTES = 1ULL << 20;
constexpr uint64_t MEDIUM_TRANSFER_MAX_BYTES = 64ULL << 20;
constexpr uint32_t SMALL_TRANSFER_PAYLOAD_BYTES = 1024;
constexpr uint32_t MEDIUM_TRANSFER_PAYLOAD_BYTES = 8192;
constexpr uint32_t LARGE_TRANSFER_PAYLOAD_BYTES = 64000;

} // namespace

uint32_t
GetSizeAwareMaximumPayloadBytes()
{
    return LARGE_TRANSFER_PAYLOAD_BYTES;
}

uint32_t
ResolveNetworkTransferPayloadBytes(const std::string& chunkMode,
                                   uint32_t fixedPayloadBytes,
                                   uint64_t transferSizeBytes)
{
    if (chunkMode != "fixed" && chunkMode != "size-aware")
    {
        throw NetworkTransferConfigError("chunk mode must be fixed or size-aware");
    }
    if (fixedPayloadBytes == 0 || fixedPayloadBytes > 65507)
    {
        throw NetworkTransferConfigError("fixed payload must be in [1, 65507] bytes");
    }
    if (transferSizeBytes == 0)
    {
        throw NetworkTransferConfigError("transfer size must be positive");
    }
    if (chunkMode == "fixed")
    {
        return fixedPayloadBytes;
    }
    if (transferSizeBytes <= SMALL_TRANSFER_MAX_BYTES)
    {
        return SMALL_TRANSFER_PAYLOAD_BYTES;
    }
    if (transferSizeBytes <= MEDIUM_TRANSFER_MAX_BYTES)
    {
        return MEDIUM_TRANSFER_PAYLOAD_BYTES;
    }
    return LARGE_TRANSFER_PAYLOAD_BYTES;
}

EcmpFlowKey
BuildNetworkTransferFlowKey(const NetworkTransfer& transfer)
{
    EcmpFlowKey flowKey;
    flowKey.sourceAddress = transfer.sourceAddress;
    flowKey.destinationAddress = transfer.destinationAddress;
    flowKey.protocol = 17;
    flowKey.sourcePort = transfer.sourcePort;
    flowKey.destinationPort = transfer.destinationPort;
    return flowKey;
}

} // namespace ns3
