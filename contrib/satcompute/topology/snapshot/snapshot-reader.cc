// 解析并校验一对节点与链路分离的 JSON 全量拓扑快照。

#include "snapshot-reader.h"

#include "../../third-party/nlohmann/json.hpp"
#include "ns3/abort.h"
#include "ns3/fatal-error.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace ns3 {

namespace {

const json*
FindField(const json& item, const std::string& name)
{
  json::const_iterator field = item.find(name);
  return field == item.end() ? nullptr : &(*field);
}

uint64_t
ParseUint64(const json& value, const std::string& field, const std::string& filename)
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
  NS_ABORT_MSG_IF(!input.is_open(), "无法打开拓扑快照: " << filename);

  try
    {
      json root;
      input >> root;
      return root;
    }
  catch (const std::exception& error)
    {
      NS_FATAL_ERROR("JSON 解析失败: " << error.what()
                                      << "\nfile: " << filename);
    }
  return json();
}

const json&
GetRequiredArray(const json& root,
                 const std::string& field,
                 const std::string& filename)
{
  NS_ABORT_MSG_IF(!root.is_object(), "JSON 根节点必须是对象: " << filename);
  const json* value = FindField(root, field);
  NS_ABORT_MSG_IF(value == nullptr, "JSON 缺少数组 " << field << ": " << filename);
  NS_ABORT_MSG_IF(!value->is_array(), field << " 必须是数组: " << filename);
  return *value;
}

std::vector<uint32_t>
ReadSatelliteNodes(const std::string& filename)
{
  json root = ReadJsonFile(filename);
  const json& nodes = GetRequiredArray(root, "nodes", filename);
  std::set<uint32_t> satelliteIds;

  for (const auto& item : nodes)
    {
      NS_ABORT_MSG_IF(!item.is_object(), "nodes 数组元素必须是对象: " << filename);
      uint32_t satelliteId = GetRequiredUint32(item, "node_id", filename);
      std::string nodeType = GetRequiredString(item, "node_type", filename);
      NS_ABORT_MSG_IF(nodeType != "sat",
                      "纯星上拓扑只允许 node_type=sat: " << filename);
      NS_ABORT_MSG_IF(!satelliteIds.insert(satelliteId).second,
                      "nodes 快照包含重复 node_id: " << satelliteId
                      << "\nfile: " << filename);
    }

  NS_ABORT_MSG_IF(satelliteIds.empty(), "nodes 快照中没有卫星: " << filename);
  return std::vector<uint32_t>(satelliteIds.begin(), satelliteIds.end());
}

std::vector<SatelliteLink>
ReadSatelliteLinks(const std::string& filename)
{
  json root = ReadJsonFile(filename);
  const json& links = GetRequiredArray(root, "links", filename);
  std::vector<SatelliteLink> parsedLinks;
  std::set<std::pair<uint32_t, uint32_t>> linkKeys;

  for (const auto& item : links)
    {
      NS_ABORT_MSG_IF(!item.is_object(), "links 数组元素必须是对象: " << filename);
      uint32_t node1Id = GetRequiredUint32(item, "node1_id", filename);
      uint32_t node2Id = GetRequiredUint32(item, "node2_id", filename);
      NS_ABORT_MSG_IF(node1Id == node2Id,
                      "卫星链路不能连接节点自身: " << node1Id
                      << "\nfile: " << filename);
      NS_ABORT_MSG_IF(GetRequiredString(item, "type", filename) != "sat",
                      "纯星上拓扑只允许 type=sat: " << filename);

      uint64_t delayUs = GetRequiredUint64(item, "delay", filename);
      NS_ABORT_MSG_IF(delayUs > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
                      "delay 超出可表示范围: " << filename);

      uint64_t bandwidthKbps =
        GetRequiredUint64(item, "link_bandwidth", filename);
      NS_ABORT_MSG_IF(bandwidthKbps == 0,
                      "link_bandwidth 必须大于 0: " << filename);
      NS_ABORT_MSG_IF(bandwidthKbps
                        > std::numeric_limits<uint64_t>::max() / 1000u,
                      "link_bandwidth 超出可表示范围: " << filename);

      uint32_t sourceId = std::min(node1Id, node2Id);
      uint32_t destinationId = std::max(node1Id, node2Id);
      std::pair<uint32_t, uint32_t> key =
        std::make_pair(sourceId, destinationId);
      NS_ABORT_MSG_IF(!linkKeys.insert(key).second,
                      "topology 快照包含重复卫星链路: "
                        << key.first << "<->" << key.second
                        << "\nfile: " << filename);

      SatelliteLink link;
      link.sourceId = sourceId;
      link.destinationId = destinationId;
      link.delayUs = delayUs;
      link.bandwidthBps = bandwidthKbps * 1000u;
      parsedLinks.push_back(link);
    }

  NS_ABORT_MSG_IF(parsedLinks.empty(), "topology 快照中没有 ISL: " << filename);
  std::sort(parsedLinks.begin(),
            parsedLinks.end(),
            [](const SatelliteLink& left, const SatelliteLink& right) {
              const std::pair<uint32_t, uint32_t> leftKey =
                std::make_pair(left.sourceId, left.destinationId);
              const std::pair<uint32_t, uint32_t> rightKey =
                std::make_pair(right.sourceId, right.destinationId);
              return leftKey < rightKey;
            });
  return parsedLinks;
}

} // namespace

SatelliteSnapshot
ReadSatelliteSnapshot(const std::string& nodesFilename,
                      const std::string& linksFilename)
{
  SatelliteSnapshot snapshot;
  snapshot.satelliteIds = ReadSatelliteNodes(nodesFilename);
  snapshot.links = ReadSatelliteLinks(linksFilename);

  std::set<uint32_t> satelliteIds(snapshot.satelliteIds.begin(),
                                  snapshot.satelliteIds.end());
  for (const auto& link : snapshot.links)
    {
      NS_ABORT_MSG_IF(satelliteIds.find(link.sourceId) == satelliteIds.end()
                        || satelliteIds.find(link.destinationId) == satelliteIds.end(),
                      "ISL 端点必须存在于配对的 nodes 快照: "
                        << link.sourceId << "<->" << link.destinationId
                        << "\nnodes: " << nodesFilename
                        << "\ntopology: " << linksFilename);
    }
  return snapshot;
}

} // namespace ns3
