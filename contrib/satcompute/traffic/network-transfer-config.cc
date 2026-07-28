/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// 解析 NetworkTransfer JSON，分配端口并推导每条传输的分包参数。

#include "network-transfer-config.h"

#include "../third-party/nlohmann/json.hpp"

#include "ns3/abort.h"
#include "ns3/fatal-error.h"
#include "ns3/nstime.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>

using json = nlohmann::json;

namespace ns3 {

namespace {

static const uint64_t SMALL_TRANSFER_MAX_BYTES = 1u << 20;
static const uint64_t MEDIUM_TRANSFER_MAX_BYTES = 64u << 20;
static const uint32_t SMALL_TRANSFER_PAYLOAD_BYTES = 1024;
static const uint32_t MEDIUM_TRANSFER_PAYLOAD_BYTES = 8192;
static const uint32_t LARGE_TRANSFER_PAYLOAD_BYTES = 64000;

const json*
FindField(const json& item, const std::string& name)
{
  json::const_iterator field = item.find(name);
  return field == item.end() ? nullptr : &(*field);
}

uint64_t
ParseUint64(const json& value,
            const std::string& field,
            const std::string& filename)
{
  try
    {
      if (value.is_number_unsigned())
        {
          return value.get<uint64_t>();
        }
      if (value.is_number_integer())
        {
          int64_t parsed = value.get<int64_t>();
          NS_ABORT_MSG_IF(parsed < 0,
                          field << " 必须是非负整数: " << filename);
          return static_cast<uint64_t>(parsed);
        }
    }
  catch (const std::exception& error)
    {
      NS_FATAL_ERROR(field << " 解析失败: " << error.what()
                           << "\nfile: " << filename);
    }

  NS_FATAL_ERROR(field << " 必须是非负整数: " << filename);
  return 0;
}

uint64_t
GetRequiredUint64(const json& item,
                  const std::string& field,
                  const std::string& filename)
{
  const json* value = FindField(item, field);
  NS_ABORT_MSG_IF(value == nullptr, "JSON 缺少字段 " << field << ": " << filename);
  return ParseUint64(*value, field, filename);
}

uint32_t
GetRequiredUint32(const json& item,
                  const std::string& field,
                  const std::string& filename)
{
  uint64_t parsed = GetRequiredUint64(item, field, filename);
  NS_ABORT_MSG_IF(parsed > std::numeric_limits<uint32_t>::max(),
                  field << " 超出 uint32 范围: " << filename);
  return static_cast<uint32_t>(parsed);
}

std::string
GetRequiredString(const json& item,
                  const std::string& field,
                  const std::string& filename)
{
  const json* value = FindField(item, field);
  NS_ABORT_MSG_IF(value == nullptr, "JSON 缺少字段 " << field << ": " << filename);
  NS_ABORT_MSG_IF(!value->is_string(), field << " 必须是字符串: " << filename);
  return value->get<std::string>();
}

json
ReadJsonFile(const std::string& filename)
{
  std::ifstream input(filename);
  NS_ABORT_MSG_IF(!input.is_open(), "无法打开 NetworkTransfer JSON: " << filename);

  try
    {
      json root;
      input >> root;
      return root;
    }
  catch (const std::exception& error)
    {
      NS_FATAL_ERROR("NetworkTransfer JSON 解析失败: " << error.what()
                                                       << "\nfile: " << filename);
    }
  return json();
}

} // namespace

NetworkTransfer::NetworkTransfer()
  : transferId(0),
    sourceSatelliteId(0),
    destinationSatelliteId(0),
    sizeBytes(0),
    arrivalTimeNs(-1),
    sourcePort(0),
    destinationPort(NETWORK_TRANSFER_DESTINATION_PORT),
    payloadBytesPerPacket(0),
    packetCount(0),
    finalPacketPayloadBytes(0)
{
}

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
  NS_ABORT_MSG_IF(chunkMode != "fixed" && chunkMode != "size-aware",
                  "transferChunkMode 必须是 fixed 或 size-aware");
  NS_ABORT_MSG_IF(fixedPayloadBytes == 0 || fixedPayloadBytes > 65507,
                  "transferPayloadBytes 必须在 1..65507 范围内");
  NS_ABORT_MSG_IF(transferSizeBytes == 0,
                  "NetworkTransfer size_bytes 必须为正整数");
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

std::vector<NetworkTransfer>
ReadNetworkTransferTrace(const std::string& filename,
                         double simulationDurationSeconds,
                         const SatelliteTopology& topology)
{
  NS_ABORT_MSG_IF(filename.empty(), "NetworkTransfer JSON 路径不能为空");

  int64_t simulationDurationNs =
    Seconds(simulationDurationSeconds).GetNanoSeconds();
  NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                  "simulationDuration 无法表示为正的纳秒时长");

  json root = ReadJsonFile(filename);
  NS_ABORT_MSG_IF(!root.is_object(),
                  "NetworkTransfer JSON 根节点必须是对象: " << filename);
  NS_ABORT_MSG_IF(root.size() != 2,
                  "NetworkTransfer JSON 根节点只允许 schema_version 和 transfers: "
                    << filename);
  NS_ABORT_MSG_IF(GetRequiredString(root, "schema_version", filename) != "0.1",
                  "NetworkTransfer schema_version 必须严格等于 0.1: " << filename);

  const json* transferItems = FindField(root, "transfers");
  NS_ABORT_MSG_IF(transferItems == nullptr || !transferItems->is_array(),
                  "NetworkTransfer JSON 缺少 transfers 数组: " << filename);

  std::vector<NetworkTransfer> transfers;
  for (const auto& item : *transferItems)
    {
      NS_ABORT_MSG_IF(!item.is_object(),
                      "transfers 数组元素必须是对象: " << filename);
      NS_ABORT_MSG_IF(item.size() != 5,
                      "transfer 只允许 transfer_id、source_node_id、"
                      "destination_node_id、size_bytes 和 arrival_time_ns: "
                        << filename);

      NetworkTransfer transfer;
      transfer.transferId = GetRequiredUint64(item, "transfer_id", filename);
      NS_ABORT_MSG_IF(transfer.transferId == 0,
                      "transfer_id 必须是正整数: " << filename);
      transfer.sourceSatelliteId =
        GetRequiredUint32(item, "source_node_id", filename);
      transfer.destinationSatelliteId =
        GetRequiredUint32(item, "destination_node_id", filename);
      NS_ABORT_MSG_IF(transfer.sourceSatelliteId
                        == transfer.destinationSatelliteId,
                      "NetworkTransfer 源卫星和目的卫星不能相同，transfer_id="
                        << transfer.transferId);
      NS_ABORT_MSG_IF(!topology.HasSatelliteId(transfer.sourceSatelliteId),
                      "NetworkTransfer 引用了未知源卫星 "
                        << transfer.sourceSatelliteId
                        << "，transfer_id=" << transfer.transferId);
      NS_ABORT_MSG_IF(!topology.HasSatelliteId(transfer.destinationSatelliteId),
                      "NetworkTransfer 引用了未知目的卫星 "
                        << transfer.destinationSatelliteId
                        << "，transfer_id=" << transfer.transferId);

      transfer.sizeBytes = GetRequiredUint64(item, "size_bytes", filename);
      NS_ABORT_MSG_IF(transfer.sizeBytes == 0,
                      "size_bytes 必须是正整数，transfer_id="
                        << transfer.transferId);

      uint64_t arrivalTimeNs =
        GetRequiredUint64(item, "arrival_time_ns", filename);
      NS_ABORT_MSG_IF(arrivalTimeNs
                        > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
                      "arrival_time_ns 超出 int64 范围，transfer_id="
                        << transfer.transferId);
      transfer.arrivalTimeNs = static_cast<int64_t>(arrivalTimeNs);
      NS_ABORT_MSG_IF(
        transfer.arrivalTimeNs >= simulationDurationNs,
        "arrival_time_ns 必须早于 simulation stop，transfer_id="
          << transfer.transferId);
      transfers.push_back(transfer);
    }

  NS_ABORT_MSG_IF(transfers.empty(),
                  "NetworkTransfer JSON 至少需要一条 transfer: " << filename);
  std::sort(transfers.begin(),
            transfers.end(),
            [](const NetworkTransfer& left, const NetworkTransfer& right) {
              return left.transferId < right.transferId;
            });

  for (uint32_t index = 0; index < transfers.size(); ++index)
    {
      NS_ABORT_MSG_IF(index > 0
                        && transfers[index - 1].transferId
                             == transfers[index].transferId,
                      "NetworkTransfer 文件包含重复 transfer_id: "
                        << transfers[index].transferId);
    }
  return transfers;
}

} // namespace ns3
