/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Parse a legacy TransferTrace and derive stable addresses, ports, and chunks.

#include "network-transfer-config.h"

#include "../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

constexpr uint64_t SMALL_TRANSFER_MAX_BYTES = 1ULL << 20;
constexpr uint64_t MEDIUM_TRANSFER_MAX_BYTES = 64ULL << 20;
constexpr uint32_t SMALL_TRANSFER_PAYLOAD_BYTES = 1024;
constexpr uint32_t MEDIUM_TRANSFER_PAYLOAD_BYTES = 8192;
constexpr uint32_t LARGE_TRANSFER_PAYLOAD_BYTES = 64000;

[[noreturn]] void
Fail(const std::filesystem::path& filename, std::string_view field, std::string_view message)
{
    throw NetworkTransferConfigError(filename.string() + ": " + std::string(field) + " " +
                                     std::string(message));
}

void
RequireObjectFields(const Json& value,
                    const std::filesystem::path& filename,
                    std::string_view name,
                    std::initializer_list<std::string_view> fields)
{
    if (!value.is_object())
    {
        Fail(filename, name, "must be an object");
    }
    std::set<std::string> expected;
    for (const std::string_view field : fields)
    {
        expected.emplace(field);
    }
    std::set<std::string> actual;
    for (const auto& item : value.items())
    {
        actual.insert(item.key());
    }
    if (actual != expected)
    {
        Fail(filename, name, "has missing or unknown fields");
    }
}

const Json&
GetField(const Json& object,
         const std::filesystem::path& filename,
         std::string_view field)
{
    const auto item = object.find(std::string(field));
    if (item == object.end())
    {
        Fail(filename, field, "is missing");
    }
    return *item;
}

uint64_t
RequireUint64(const Json& value,
              const std::filesystem::path& filename,
              std::string_view field)
{
    if (value.is_number_unsigned())
    {
        return value.get<uint64_t>();
    }
    if (value.is_number_integer())
    {
        const int64_t parsed = value.get<int64_t>();
        if (parsed >= 0)
        {
            return static_cast<uint64_t>(parsed);
        }
    }
    Fail(filename, field, "must be a non-negative integer");
}

uint32_t
RequireUint32(const Json& value,
              const std::filesystem::path& filename,
              std::string_view field)
{
    const uint64_t parsed = RequireUint64(value, filename, field);
    if (parsed > std::numeric_limits<uint32_t>::max())
    {
        Fail(filename, field, "exceeds uint32 range");
    }
    return static_cast<uint32_t>(parsed);
}

Json
ReadJson(const std::filesystem::path& filename)
{
    std::ifstream input(filename);
    if (!input.is_open())
    {
        Fail(filename, "file", "cannot be opened");
    }
    try
    {
        return Json::parse(input, nullptr, true, false);
    }
    catch (const std::exception& error)
    {
        Fail(filename, "JSON", error.what());
    }
}

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

std::vector<NetworkTransfer>
ReadNetworkTransferTrace(const std::filesystem::path& filename,
                         int64_t simulationDurationNs,
                         const std::string& chunkMode,
                         uint32_t fixedPayloadBytes,
                         const SatelliteEndpointView& endpoints)
{
    if (filename.empty())
    {
        throw NetworkTransferConfigError("transfer trace path must not be empty");
    }
    if (simulationDurationNs <= 0)
    {
        throw NetworkTransferConfigError("simulation duration must be positive integer ns");
    }
    // Validate the chunk contract even if a malformed trace contains no item.
    ResolveNetworkTransferPayloadBytes(chunkMode, fixedPayloadBytes, 1);

    const Json root = ReadJson(filename);
    RequireObjectFields(root, filename, "root", {"schema_version", "transfers"});
    const Json& schemaVersion = GetField(root, filename, "schema_version");
    if (!schemaVersion.is_string() || schemaVersion.get<std::string>() != "0.1")
    {
        Fail(filename, "schema_version", "must equal 0.1");
    }
    const Json& items = GetField(root, filename, "transfers");
    if (!items.is_array() || items.empty())
    {
        Fail(filename, "transfers", "must be a non-empty array");
    }

    std::vector<NetworkTransfer> transfers;
    transfers.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const Json& item = items[index];
        const std::string itemName = "transfers[" + std::to_string(index) + "]";
        RequireObjectFields(item,
                            filename,
                            itemName,
                            {"transfer_id",
                             "source_node_id",
                             "destination_node_id",
                             "size_bytes",
                             "arrival_time_ns"});

        NetworkTransfer transfer;
        transfer.transferId =
            RequireUint64(GetField(item, filename, "transfer_id"), filename, "transfer_id");
        transfer.sourceSatelliteId = RequireUint32(
            GetField(item, filename, "source_node_id"),
            filename,
            "source_node_id");
        transfer.destinationSatelliteId = RequireUint32(
            GetField(item, filename, "destination_node_id"),
            filename,
            "destination_node_id");
        transfer.sizeBytes =
            RequireUint64(GetField(item, filename, "size_bytes"), filename, "size_bytes");
        const uint64_t arrivalTimeNs = RequireUint64(
            GetField(item, filename, "arrival_time_ns"),
            filename,
            "arrival_time_ns");

        if (transfer.transferId == 0)
        {
            Fail(filename, "transfer_id", "must be positive");
        }
        if (transfer.sourceSatelliteId == transfer.destinationSatelliteId)
        {
            Fail(filename, itemName, "must use different source and destination satellites");
        }
        if (!endpoints.HasSatelliteId(transfer.sourceSatelliteId) ||
            !endpoints.HasSatelliteId(transfer.destinationSatelliteId))
        {
            Fail(filename, itemName, "references an unknown satellite ID");
        }
        if (transfer.sizeBytes == 0)
        {
            Fail(filename, "size_bytes", "must be positive");
        }
        if (arrivalTimeNs >= static_cast<uint64_t>(simulationDurationNs))
        {
            Fail(filename, "arrival_time_ns", "must be earlier than simulation stop");
        }
        transfer.arrivalTimeNs = static_cast<int64_t>(arrivalTimeNs);
        transfers.push_back(transfer);
    }

    std::sort(transfers.begin(),
              transfers.end(),
              [](const NetworkTransfer& left, const NetworkTransfer& right) {
                  return left.transferId < right.transferId;
              });

    std::map<uint32_t, uint32_t> nextSourceOrdinal;
    for (std::size_t index = 0; index < transfers.size(); ++index)
    {
        NetworkTransfer& transfer = transfers[index];
        if (index > 0 && transfers[index - 1].transferId == transfer.transferId)
        {
            Fail(filename, "transfer_id", "must be unique");
        }

        const uint32_t ordinal = nextSourceOrdinal[transfer.sourceSatelliteId];
        if (ordinal > std::numeric_limits<uint16_t>::max() -
                          NETWORK_TRANSFER_FIRST_SOURCE_PORT)
        {
            Fail(filename, "source_node_id", "has exhausted the UDP source-port range");
        }
        ++nextSourceOrdinal[transfer.sourceSatelliteId];
        transfer.sourceAddress = endpoints.GetServiceAddress(transfer.sourceSatelliteId);
        transfer.destinationAddress = endpoints.GetServiceAddress(transfer.destinationSatelliteId);
        transfer.sourcePort =
            static_cast<uint16_t>(NETWORK_TRANSFER_FIRST_SOURCE_PORT + ordinal);
        transfer.destinationPort = NETWORK_TRANSFER_DESTINATION_PORT;
        transfer.payloadBytesPerPacket = ResolveNetworkTransferPayloadBytes(
            chunkMode,
            fixedPayloadBytes,
            transfer.sizeBytes);
        transfer.packetCount = transfer.sizeBytes / transfer.payloadBytesPerPacket +
                               (transfer.sizeBytes % transfer.payloadBytesPerPacket == 0 ? 0 : 1);
        transfer.finalPacketPayloadBytes =
            transfer.sizeBytes % transfer.payloadBytesPerPacket == 0
                ? transfer.payloadBytesPerPacket
                : static_cast<uint32_t>(transfer.sizeBytes % transfer.payloadBytesPerPacket);
    }
    return transfers;
}

} // namespace ns3
