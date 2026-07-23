#include "topo-json.h"
#include "../../sdn-controller/json.hpp"
#include "ns3/fatal-error.h"
#include "ns3/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

using json = nlohmann::json;

namespace ns3 {

static std::string
BaseName(const std::string& path)
{
  std::string::size_type pos = path.find_last_of('/');
  if (pos == std::string::npos)
  {
    return path;
  }
  return path.substr(pos + 1);
}

static std::string
DirectoryName(const std::string& path)
{
  std::string::size_type pos = path.find_last_of('/');
  if (pos == std::string::npos)
  {
    return "";
  }
  return path.substr(0, pos + 1);
}

static bool
IsAbsolutePath(const std::string& path)
{
  return !path.empty() && path[0] == '/';
}

static std::string
ResolveRelativePath(const std::string& baseFile, const std::string& path)
{
  if (path.empty() || IsAbsolutePath(path))
  {
    return path;
  }
  return DirectoryName(baseFile) + path;
}

static std::string
JoinPath(const std::string& dir, const std::string& file)
{
  if (dir.empty() || dir[dir.size() - 1] == '/')
  {
    return dir + file;
  }
  return dir + "/" + file;
}

static bool
StartsWith(const std::string& value, const std::string& prefix)
{
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool
EndsWith(const std::string& value, const std::string& suffix)
{
  return value.size() >= suffix.size()
         && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool
TryParseSecondsFromTimeSliceFilename(const std::string& path, double& seconds)
{
  // 支持甲方给出的 xxx_5s.json / xxx_10s.json 命名规则。
  // 前缀由调用方判断，这里只负责从最后一个下划线后的xs提取秒数。
  std::string name = BaseName(path);
  std::string::size_type dot = name.find_last_of('.');
  if (dot != std::string::npos)
  {
    name = name.substr(0, dot);
  }

  std::string::size_type underscore = name.find_last_of('_');
  if (underscore == std::string::npos || underscore + 2 > name.size())
  {
    return false;
  }

  std::string suffix = name.substr(underscore + 1);
  if (suffix.empty() || suffix[suffix.size() - 1] != 's')
  {
    return false;
  }

  std::string value = suffix.substr(0, suffix.size() - 1);
  if (value.empty())
  {
    return false;
  }

  for (char c : value)
  {
    if (!((c >= '0' && c <= '9') || c == '.'))
    {
      return false;
    }
  }

  try
  {
    seconds = std::stod(value);
  }
  catch (const std::exception&)
  {
    return false;
  }
  return true;
}

static bool
HasLinkOutputTimestampShape(const std::string& value)
{
  if (value.size() != 19
      || value[4] != '-'
      || value[7] != '-'
      || value[10] != '_'
      || value[13] != '-'
      || value[16] != '-')
  {
    return false;
  }

  for (uint32_t i = 0; i < value.size(); ++i)
  {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16)
    {
      continue;
    }
    if (value[i] < '0' || value[i] > '9')
    {
      return false;
    }
  }
  return true;
}

static uint32_t
ParseTimestampNumber(const std::string& value, uint32_t position, uint32_t length)
{
  uint32_t result = 0;
  for (uint32_t i = 0; i < length; ++i)
  {
    result = result * 10 + static_cast<uint32_t>(value[position + i] - '0');
  }
  return result;
}

static bool
IsLeapYear(uint32_t year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool
TryParseLinkOutputTimestamp(const std::string& value, int64_t& timestampSeconds)
{
  if (!HasLinkOutputTimestampShape(value))
  {
    return false;
  }

  uint32_t year = ParseTimestampNumber(value, 0, 4);
  uint32_t month = ParseTimestampNumber(value, 5, 2);
  uint32_t day = ParseTimestampNumber(value, 8, 2);
  uint32_t hour = ParseTimestampNumber(value, 11, 2);
  uint32_t minute = ParseTimestampNumber(value, 14, 2);
  uint32_t second = ParseTimestampNumber(value, 17, 2);
  if (year == 0 || month == 0 || month > 12 || hour > 23 || minute > 59 || second > 59)
  {
    return false;
  }

  static const uint32_t daysInMonth[] =
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint32_t maxDay = daysInMonth[month - 1];
  if (month == 2 && IsLeapYear(year))
  {
    maxDay = 29;
  }
  if (day == 0 || day > maxDay)
  {
    return false;
  }

  uint32_t completedYears = year - 1;
  int64_t days = static_cast<int64_t>(completedYears) * 365
                 + completedYears / 4
                 - completedYears / 100
                 + completedYears / 400;
  for (uint32_t currentMonth = 1; currentMonth < month; ++currentMonth)
  {
    days += daysInMonth[currentMonth - 1];
    if (currentMonth == 2 && IsLeapYear(year))
    {
      ++days;
    }
  }
  days += day - 1;
  timestampSeconds = days * 24 * 60 * 60
                     + static_cast<int64_t>(hour) * 60 * 60
                     + static_cast<int64_t>(minute) * 60
                     + second;
  return true;
}

static const json*
FindJsonField(const json& item, const std::vector<std::string>& keys)
{
  // 需求文档和样例里出现过少量字段拼写差异。
  // 这里用候选字段名兼容输入格式，避免把兼容逻辑散落到业务代码中。
  for (const auto& key : keys)
  {
    auto it = item.find(key);
    if (it != item.end())
    {
      return &(*it);
    }
  }
  return nullptr;
}

static bool
JsonToUint32(const json& value, uint32_t& out)
{
  try
  {
    if (value.is_number_unsigned())
    {
      out = value.get<uint32_t>();
      return true;
    }
    if (value.is_number_integer())
    {
      int64_t v = value.get<int64_t>();
      if (v < 0 || v > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
      {
        return false;
      }
      out = static_cast<uint32_t>(v);
      return true;
    }
    if (value.is_number_float())
    {
      double v = value.get<double>();
      if (v < 0.0 || v > static_cast<double>(std::numeric_limits<uint32_t>::max()))
      {
        return false;
      }
      out = static_cast<uint32_t>(v);
      return true;
    }
    if (value.is_boolean())
    {
      out = value.get<bool>() ? 1 : 0;
      return true;
    }
    if (value.is_string())
    {
      out = static_cast<uint32_t>(std::stoul(value.get<std::string>()));
      return true;
    }
  }
  catch (const std::exception&)
  {
    return false;
  }
  return false;
}

static uint32_t
GetRequiredUint32Field(const json& item, const std::vector<std::string>& keys)
{
  const json* value = FindJsonField(item, keys);
  if (value == nullptr)
  {
    NS_FATAL_ERROR("JSON缺少必填字段");
  }
  uint32_t out = 0;
  if (!JsonToUint32(*value, out))
  {
    NS_FATAL_ERROR("JSON字段无法解析为非负整数");
  }
  return out;
}

static uint32_t
GetUint32Field(const json& item, const std::vector<std::string>& keys, uint32_t defaultValue)
{
  const json* value = FindJsonField(item, keys);
  if (value == nullptr)
  {
    return defaultValue;
  }
  uint32_t out = defaultValue;
  if (!JsonToUint32(*value, out))
  {
    NS_FATAL_ERROR("JSON字段无法解析为非负整数");
  }
  return out;
}

static bool
GetBoolField(const json& item, const std::vector<std::string>& keys, bool defaultValue)
{
  const json* value = FindJsonField(item, keys);
  if (value == nullptr)
  {
    return defaultValue;
  }
  if (value->is_boolean())
  {
    return value->get<bool>();
  }
  uint32_t out = defaultValue ? 1 : 0;
  if (!JsonToUint32(*value, out))
  {
    NS_FATAL_ERROR("JSON字段无法解析为布尔值");
  }
  return out != 0;
}

static std::string
GetStringField(const json& item, const std::vector<std::string>& keys, const std::string& defaultValue)
{
  const json* value = FindJsonField(item, keys);
  if (value == nullptr)
  {
    return defaultValue;
  }
  if (value->is_string())
  {
    return value->get<std::string>();
  }
  if (value->is_number_integer())
  {
    return std::to_string(value->get<int64_t>());
  }
  if (value->is_number_unsigned())
  {
    return std::to_string(value->get<uint64_t>());
  }
  return defaultValue;
}

static double
GetDoubleField(const json& item, const std::vector<std::string>& keys, double defaultValue)
{
  const json* value = FindJsonField(item, keys);
  if (value == nullptr)
  {
    return defaultValue;
  }
  try
  {
    if (value->is_number())
    {
      return value->get<double>();
    }
    if (value->is_string())
    {
      return std::stod(value->get<std::string>());
    }
  }
  catch (const std::exception&)
  {
    NS_FATAL_ERROR("JSON字段无法解析为浮点数");
  }
  NS_FATAL_ERROR("JSON字段无法解析为浮点数");
  return defaultValue;
}

static json
ReadJsonFile(const std::string& filename)
{
  std::ifstream file(filename);
  if (!file.is_open())
  {
    NS_FATAL_ERROR("无法打开JSON文件: " << filename);
  }
  json data;
  try
  {
    file >> data;
  }
  catch (const std::exception& error)
  {
    NS_FATAL_ERROR("JSON文件解析失败: " << filename << "\n  " << error.what());
  }
  return data;
}

static json
ExtractJsonArray(const json& root, const std::vector<std::string>& keys)
{
  if (root.is_array())
  {
    return root;
  }
  for (const auto& key : keys)
  {
    auto it = root.find(key);
    if (it != root.end() && it->is_array())
    {
      return *it;
    }
  }
  NS_FATAL_ERROR("JSON文件中未找到数组字段");
  return json::array();
}

static TopologyNodeInfo
ParseTopologyNodeJson(const json& item)
{
  // 字段候选列表中的 hode_id/hode_ype 等拼写用于兼容早期需求文档中的笔误。
  TopologyNodeInfo info;
  info.node_id = GetRequiredUint32Field(item, {"node_id", "hode_id", "id"});
  info.node_type = GetStringField(item, {"node_type", "hode_ype", "type"}, "sat");
  info.is_cluster = GetBoolField(item, {"is_cluster", "s_cluster"}, false);
  info.cluster_id = GetUint32Field(item, {"cluster_id", "chuster id", "cluster id"}, 0);
  info.is_cluster_head = GetBoolField(item, {"is_clusterhead", "is_cluster_head", "s_clusterhcad"}, false);
  return info;
}

std::vector<TopologyNodeInfo>
ReadTopologyNodesJsonFile(const std::string& filename)
{
  json root = ReadJsonFile(filename);
  json nodesArray = ExtractJsonArray(root, {"nodes", "data"});
  std::vector<TopologyNodeInfo> nodes;

  for (const auto& item : nodesArray)
  {
    nodes.push_back(ParseTopologyNodeJson(item));
  }

  return nodes;
}

static LinkInfo
ParseTopologyLinkJson(const json& item,
                      const TopologyNodeResolver& resolver,
                      bool zeroBasedFallback,
                      bool linkOutputFormat = false)
{
  uint32_t node1 = GetRequiredUint32Field(item, {"node1_id", "nodel_id", "node1 id", "source", "src"});
  uint32_t node2 = GetRequiredUint32Field(item, {"node2_id", "node2 id", "destination", "dst", "dest"});

  LinkInfo link;
  // JSON中的节点ID可以是外部node_id，也可以在没有nodes文件时退化为0/1基下标。
  // resolver由topo.cc提供，因此解析层不依赖NodeContainer。
  link.source = resolver(node1, zeroBasedFallback);
  link.destination = resolver(node2, zeroBasedFallback);
  link.type = GetStringField(item, {"type", "link_type"}, "sat");
  link.has_type = FindJsonField(item, {"type", "link_type"}) != nullptr;
  if (linkOutputFormat && FindJsonField(item, {"delay"}) != nullptr)
  {
    double delayMs = GetDoubleField(item, {"delay"}, 0.0);
    double delayUs = delayMs * 1000.0;
    if (!std::isfinite(delayUs)
        || delayUs < 0.0
        || delayUs > static_cast<double>(std::numeric_limits<uint32_t>::max()))
    {
      NS_FATAL_ERROR("link_output delay超出可解析范围");
    }
    link.delay_us = static_cast<uint32_t>(std::llround(delayUs));
    link.delay_ms = static_cast<uint32_t>(std::ceil(delayMs));
    link.has_delay_us = true;
    link.has_delay_ms = false;
  }
  else
  {
    link.delay_us = GetUint32Field(item, {"delay", "delay_us"}, 0);
    link.has_delay_us = FindJsonField(item, {"delay", "delay_us"}) != nullptr;
    link.has_delay_ms = FindJsonField(item, {"delay_ms"}) != nullptr;
    link.delay_ms =
      link.has_delay_us ? (link.delay_us + 999) / 1000 : GetUint32Field(item, {"delay_ms"}, 0);
  }
  link.link_bandwidth_kbps = GetUint32Field(item, {"link_bandwidth", "link_bandwidth_kbps"}, 0);
  link.has_link_bandwidth_kbps = FindJsonField(item, {"link_bandwidth", "link_bandwidth_kbps"}) != nullptr;
  link.has_bandwidth_gbps = FindJsonField(item, {"bandwidth_gbps"}) != nullptr;
  link.bandwidth_gbps = link.has_link_bandwidth_kbps ? 0 : GetUint32Field(item, {"bandwidth_gbps"}, 0);
  link.link_load_up_kbps = GetUint32Field(item, {"link_load_up", "link_load_up_kbps"}, 0);
  link.has_link_load_up_kbps = FindJsonField(item, {"link_load_up", "link_load_up_kbps"}) != nullptr;
  link.link_load_down_kbps = GetUint32Field(item, {"link_load_down", "link_load_down_kbps"}, 0);
  link.has_link_load_down_kbps = FindJsonField(item, {"link_load_down", "link_load_down_kbps"}) != nullptr;
  link.hold_time_s = GetUint32Field(item, {"hold_time", "hold_time_s"}, 0);
  link.has_hold_time_s = FindJsonField(item, {"hold_time", "hold_time_s"}) != nullptr;
  return link;
}

static std::vector<LinkInfo>
ParseTopologyLinksJsonArray(const json& linksArray,
                            const TopologyNodeResolver& resolver,
                            bool hasExplicitNodeIds,
                            bool linkOutputFormat = false)
{
  std::vector<LinkInfo> links;
  bool zeroBasedFallback = false;
  if (!hasExplicitNodeIds)
  {
    // 如果没有显式nodes文件，链路文件可能直接使用节点下标。
    // 出现0说明更可能是0基下标，否则按传统CSV兼容为1基下标。
    for (const auto& item : linksArray)
    {
      uint32_t node1 = GetRequiredUint32Field(item, {"node1_id", "nodel_id", "node1 id", "source", "src"});
      uint32_t node2 = GetRequiredUint32Field(item, {"node2_id", "node2 id", "destination", "dst", "dest"});
      if (node1 == 0 || node2 == 0)
      {
        zeroBasedFallback = true;
        break;
      }
    }
  }
  for (const auto& item : linksArray)
  {
    links.push_back(ParseTopologyLinkJson(item,
                                          resolver,
                                          zeroBasedFallback,
                                          linkOutputFormat));
  }
  return links;
}

std::vector<LinkInfo>
ReadTopologyLinksJsonFile(const std::string& filename,
                          const TopologyNodeResolver& resolver,
                          bool hasExplicitNodeIds)
{
  json root = ReadJsonFile(filename);
  json linksArray = ExtractJsonArray(root, {"links", "data"});
  return ParseTopologyLinksJsonArray(linksArray, resolver, hasExplicitNodeIds);
}

struct LinkOutputJsonItems
{
  json links;
  json satellites;

  LinkOutputJsonItems()
    : links(json::array()),
      satellites(json::array())
  {
  }
};

static LinkOutputJsonItems
ClassifyLinkOutputJsonItems(const json& root, const std::string& filename)
{
  if (!root.is_array())
  {
    NS_FATAL_ERROR("link_output文件顶层必须是数组: " << filename);
  }

  LinkOutputJsonItems result;
  for (const auto& item : root)
  {
    if (!item.is_object())
    {
      NS_FATAL_ERROR("link_output数组元素必须是对象: " << filename);
    }

    bool isLink = FindJsonField(item, {"node1_id", "node2_id"}) != nullptr;
    bool isSatellite = FindJsonField(item, {"sat_id", "clusterId"}) != nullptr;
    if (isLink == isSatellite)
    {
      NS_FATAL_ERROR("link_output包含无法区分的数组元素: " << filename);
    }
    if (isLink)
    {
      result.links.push_back(item);
    }
    else
    {
      result.satellites.push_back(item);
    }
  }
  return result;
}

struct LinkOutputSatelliteJsonInfo
{
  TopologyNodeInfo node;
  bool has_is_cluster;
  bool has_is_cluster_head;

  LinkOutputSatelliteJsonInfo()
    : has_is_cluster(false),
      has_is_cluster_head(false)
  {
  }
};

static std::map<uint32_t, LinkOutputSatelliteJsonInfo>
ParseLinkOutputSatelliteInfos(const json& satellites, const std::string& filename)
{
  std::map<uint32_t, LinkOutputSatelliteJsonInfo> satelliteInfos;
  for (const auto& item : satellites)
  {
    uint32_t satelliteId = GetRequiredUint32Field(item, {"sat_id"});
    LinkOutputSatelliteJsonInfo parsed;
    parsed.node.node_id = satelliteId;
    parsed.node.node_type = GetStringField(item, {"node_type", "type"}, "sat");
    parsed.node.is_cluster = GetBoolField(item, {"is_cluster"}, true);
    parsed.node.cluster_id = GetRequiredUint32Field(item, {"clusterId", "cluster_id"});
    parsed.node.is_cluster_head =
      GetBoolField(item, {"is_cluster_head", "is_clusterhead"}, false);
    parsed.has_is_cluster = FindJsonField(item, {"is_cluster"}) != nullptr;
    parsed.has_is_cluster_head =
      FindJsonField(item, {"is_cluster_head", "is_clusterhead"}) != nullptr;
    if (!satelliteInfos.insert(std::make_pair(satelliteId, parsed)).second)
    {
      NS_FATAL_ERROR("link_output卫星ID重复: " << satelliteId << "\n  file: " << filename);
    }
  }
  if (satelliteInfos.empty())
  {
    NS_FATAL_ERROR("link_output文件没有sat_id节点项: " << filename);
  }
  return satelliteInfos;
}

std::vector<TopologyNodeInfo>
ReadLinkOutputInitialNodesJsonFile(const std::string& filename)
{
  LinkOutputJsonItems items = ClassifyLinkOutputJsonItems(ReadJsonFile(filename), filename);
  std::map<uint32_t, LinkOutputSatelliteJsonInfo> satelliteInfos =
    ParseLinkOutputSatelliteInfos(items.satellites, filename);
  std::set<uint32_t> satelliteIds;
  std::set<uint32_t> groundIds;
  for (const auto& item : satelliteInfos)
  {
    satelliteIds.insert(item.first);
  }

  for (const auto& item : items.links)
  {
    uint32_t node1 = GetRequiredUint32Field(item, {"node1_id"});
    uint32_t node2 = GetRequiredUint32Field(item, {"node2_id"});
    if (node1 == node2)
    {
      NS_FATAL_ERROR("link_output链路不能连接同一节点: " << node1 << "\n  file: " << filename);
    }

    std::string type = GetStringField(item, {"type", "link_type"}, "sat");
    if (type != "feeder")
    {
      continue;
    }
    bool node1IsSatellite = satelliteIds.find(node1) != satelliteIds.end();
    bool node2IsSatellite = satelliteIds.find(node2) != satelliteIds.end();
    if (node1IsSatellite == node2IsSatellite)
    {
      NS_FATAL_ERROR("link_output feeder必须恰好连接一颗卫星和一个地面站"
                     << "\n  link: " << node1 << "<->" << node2
                     << "\n  file: " << filename);
    }
    groundIds.insert(node1IsSatellite ? node2 : node1);
  }

  std::set<uint32_t> knownNodeIds = satelliteIds;
  knownNodeIds.insert(groundIds.begin(), groundIds.end());
  for (const auto& item : items.links)
  {
    uint32_t node1 = GetRequiredUint32Field(item, {"node1_id"});
    uint32_t node2 = GetRequiredUint32Field(item, {"node2_id"});
    if (knownNodeIds.find(node1) == knownNodeIds.end()
        || knownNodeIds.find(node2) == knownNodeIds.end())
    {
      NS_FATAL_ERROR("link_output链路引用了无法从sat_id或feeder推导的节点"
                     << "\n  link: " << node1 << "<->" << node2
                     << "\n  file: " << filename);
    }
  }

  std::vector<TopologyNodeInfo> nodes;
  nodes.reserve(satelliteIds.size() + groundIds.size());
  for (const auto& item : satelliteInfos)
  {
    nodes.push_back(item.second.node);
  }
  for (uint32_t groundId : groundIds)
  {
    TopologyNodeInfo info;
    info.node_id = groundId;
    info.node_type = "ground";
    nodes.push_back(info);
  }
  return nodes;
}

LinkOutputSnapshot
ReadLinkOutputSnapshotJsonFile(const std::string& filename,
                               const TopologyNodeResolver& resolver)
{
  LinkOutputJsonItems items = ClassifyLinkOutputJsonItems(ReadJsonFile(filename), filename);
  std::map<uint32_t, LinkOutputSatelliteJsonInfo> satelliteInfos =
    ParseLinkOutputSatelliteInfos(items.satellites, filename);

  LinkOutputSnapshot snapshot;
  for (const auto& item : satelliteInfos)
  {
    TopologyNodePatch patch;
    patch.node_id = item.first;
    patch.cluster_id = item.second.node.cluster_id;
    patch.has_cluster_id = true;
    if (item.second.has_is_cluster)
    {
      patch.is_cluster = item.second.node.is_cluster;
      patch.has_is_cluster = true;
    }
    if (item.second.has_is_cluster_head)
    {
      patch.is_cluster_head = item.second.node.is_cluster_head;
      patch.has_is_cluster_head = true;
    }
    snapshot.node_updates.push_back(patch);
  }
  snapshot.links = ParseTopologyLinksJsonArray(items.links, resolver, true, true);
  return snapshot;
}

static json
GetArrayFieldOrEmpty(const json& root, const std::vector<std::string>& keys)
{
  if (root.is_array())
  {
    return root;
  }
  if (!root.is_object())
  {
    return json::array();
  }
  for (const auto& key : keys)
  {
    auto it = root.find(key);
    if (it != root.end() && it->is_array())
    {
      return *it;
    }
  }
  return json::array();
}

static TopologyNodePatch
ParseTopologyNodePatchJson(const json& item)
{
  TopologyNodePatch patch;
  patch.node_id = GetRequiredUint32Field(item, {"node_id", "hode_id", "id"});

  if (FindJsonField(item, {"is_cluster", "s_cluster"}) != nullptr)
  {
    patch.is_cluster = GetBoolField(item, {"is_cluster", "s_cluster"}, false);
    patch.has_is_cluster = true;
  }
  if (FindJsonField(item, {"cluster_id", "chuster id", "cluster id"}) != nullptr)
  {
    patch.cluster_id = GetUint32Field(item, {"cluster_id", "chuster id", "cluster id"}, 0);
    patch.has_cluster_id = true;
  }
  if (FindJsonField(item, {"is_clusterhead", "is_cluster_head", "s_clusterhcad"}) != nullptr)
  {
    patch.is_cluster_head = GetBoolField(item,
                                         {"is_clusterhead", "is_cluster_head", "s_clusterhcad"},
                                         false);
    patch.has_is_cluster_head = true;
  }

  return patch;
}

static bool
DetectZeroBasedLinkIds(const json& items, bool hasExplicitNodeIds)
{
  if (hasExplicitNodeIds)
  {
    return false;
  }
  for (const auto& item : items)
  {
    uint32_t node1 = GetRequiredUint32Field(item, {"node1_id", "nodel_id", "node1 id", "source", "src"});
    uint32_t node2 = GetRequiredUint32Field(item, {"node2_id", "node2 id", "destination", "dst", "dest"});
    if (node1 == 0 || node2 == 0)
    {
      return true;
    }
  }
  return false;
}

static TopologyLinkRemove
ParseTopologyLinkRemoveJson(const json& item,
                            const TopologyNodeResolver& resolver,
                            bool zeroBasedFallback)
{
  uint32_t node1 = GetRequiredUint32Field(item, {"node1_id", "nodel_id", "node1 id", "source", "src"});
  uint32_t node2 = GetRequiredUint32Field(item, {"node2_id", "node2 id", "destination", "dst", "dest"});

  TopologyLinkRemove remove;
  remove.source = resolver(node1, zeroBasedFallback);
  remove.destination = resolver(node2, zeroBasedFallback);
  return remove;
}

static void
AppendTopologyNodePatches(const json& items, TopologyPatchInfo& patch)
{
  for (const auto& item : items)
  {
    patch.node_updates.push_back(ParseTopologyNodePatchJson(item));
  }
}

static void
AppendTopologyLinkUpserts(const json& items,
                          const TopologyNodeResolver& resolver,
                          bool hasExplicitNodeIds,
                          TopologyPatchInfo& patch)
{
  std::vector<LinkInfo> links = ParseTopologyLinksJsonArray(items, resolver, hasExplicitNodeIds);
  patch.link_upserts.insert(patch.link_upserts.end(), links.begin(), links.end());
}

static void
AppendTopologyLinkRemoves(const json& items,
                          const TopologyNodeResolver& resolver,
                          bool hasExplicitNodeIds,
                          TopologyPatchInfo& patch)
{
  bool zeroBasedFallback = DetectZeroBasedLinkIds(items, hasExplicitNodeIds);
  for (const auto& item : items)
  {
    patch.link_removes.push_back(ParseTopologyLinkRemoveJson(item, resolver, zeroBasedFallback));
  }
}

static TopologyPatchInfo
ParseTopologyPatchJsonObject(const json& root,
                             const TopologyNodeResolver& resolver,
                             bool hasExplicitNodeIds)
{
  if (!root.is_object())
  {
    NS_FATAL_ERROR("patch JSON必须是对象，且包含nodes/links修改项");
  }

  TopologyPatchInfo patch;

  auto nodesIt = root.find("nodes");
  if (nodesIt != root.end())
  {
    if (nodesIt->is_array())
    {
      AppendTopologyNodePatches(*nodesIt, patch);
    }
    else if (nodesIt->is_object())
    {
      AppendTopologyNodePatches(GetArrayFieldOrEmpty(*nodesIt, {"update", "updates", "upsert"}), patch);
    }
  }
  AppendTopologyNodePatches(GetArrayFieldOrEmpty(root, {"node_updates", "nodes_update"}), patch);

  auto linksIt = root.find("links");
  if (linksIt != root.end())
  {
    if (linksIt->is_array())
    {
      AppendTopologyLinkUpserts(*linksIt, resolver, hasExplicitNodeIds, patch);
    }
    else if (linksIt->is_object())
    {
      AppendTopologyLinkUpserts(GetArrayFieldOrEmpty(*linksIt, {"upsert", "upserts", "update", "updates", "add", "adds"}),
                                resolver,
                                hasExplicitNodeIds,
                                patch);
      AppendTopologyLinkRemoves(GetArrayFieldOrEmpty(*linksIt, {"remove", "removes", "delete", "deletes", "down"}),
                                resolver,
                                hasExplicitNodeIds,
                                patch);
    }
  }
  AppendTopologyLinkUpserts(GetArrayFieldOrEmpty(root, {"link_upserts", "links_upsert"}),
                            resolver,
                            hasExplicitNodeIds,
                            patch);
  AppendTopologyLinkRemoves(GetArrayFieldOrEmpty(root, {"link_removes", "links_remove"}),
                            resolver,
                            hasExplicitNodeIds,
                            patch);

  return patch;
}

TopologyPatchInfo
ReadTopologyPatchJsonFile(const std::string& filename,
                          const TopologyNodeResolver& resolver,
                          bool hasExplicitNodeIds)
{
  json root = ReadJsonFile(filename);
  return ParseTopologyPatchJsonObject(root, resolver, hasExplicitNodeIds);
}

std::vector<TopologyTimeSlice>
ReadTopologyTimeSlicesJsonFile(const std::string& filename,
                               const TopologyNodeResolver& resolver,
                               bool hasExplicitNodeIds,
                               bool patchMode)
{
  json root = ReadJsonFile(filename);
  json slicesArray = ExtractJsonArray(root, {"time_slices", "timeslices", "slices", "data"});
  std::vector<TopologyTimeSlice> slices;
  for (const auto& item : slicesArray)
  {
    TopologyTimeSlice slice;
    slice.is_patch = patchMode;
    if (patchMode)
    {
      slice.patch_file = GetStringField(item, {"patch_file", "file"}, "");
      slice.patch_file = ResolveRelativePath(filename, slice.patch_file);
    }
    else
    {
      slice.nodes_file = GetStringField(item, {"nodes_file", "node_file"}, "");
      slice.nodes_file = ResolveRelativePath(filename, slice.nodes_file);
      slice.links_file = GetStringField(item, {"links_file", "topology_file", "file"}, "");
      slice.links_file = ResolveRelativePath(filename, slice.links_file);
      slice.has_nodes_update = !slice.nodes_file.empty();
      slice.has_links_update = !slice.links_file.empty();
    }

    if (FindJsonField(item, {"time", "time_s", "sim_time"}) != nullptr)
    {
      slice.time_s = GetDoubleField(item, {"time", "time_s", "sim_time"}, 0.0);
    }
    else if (!slice.patch_file.empty() && TryParseSecondsFromTimeSliceFilename(slice.patch_file, slice.time_s))
    {
    }
    else if (!slice.links_file.empty() && TryParseSecondsFromTimeSliceFilename(slice.links_file, slice.time_s))
    {
    }
    else if (!slice.nodes_file.empty() && TryParseSecondsFromTimeSliceFilename(slice.nodes_file, slice.time_s))
    {
    }
    else
    {
      NS_FATAL_ERROR("时间片缺少time字段，且文件名无法解析为xxx_5s.json格式");
    }

    if (patchMode)
    {
      if (slice.patch_file.empty())
      {
        slice.patch = ParseTopologyPatchJsonObject(item, resolver, hasExplicitNodeIds);
      }
    }
    else
    {
      auto nodesIt = item.find("nodes");
      if (nodesIt != item.end() && nodesIt->is_array())
      {
        slice.has_nodes_update = true;
        for (const auto& nodeItem : *nodesIt)
        {
          slice.nodes.push_back(ParseTopologyNodeJson(nodeItem));
        }
      }

      auto linksIt = item.find("links");
      if (linksIt != item.end() && linksIt->is_array())
      {
        slice.has_links_update = true;
        slice.links = ParseTopologyLinksJsonArray(*linksIt, resolver, hasExplicitNodeIds);
      }
    }

    slices.push_back(slice);
  }
  std::sort(slices.begin(), slices.end(), [](const TopologyTimeSlice& lhs, const TopologyTimeSlice& rhs) {
    return lhs.time_s < rhs.time_s;
  });
  return slices;
}

std::vector<TopologyTimeSlice>
ScanTopologyTimeSlicesDirectory(const std::string& dirname, bool patchMode)
{
  // 默认目录模式按文件名配对时间片：
  // nodes_5s.json 与 topology_5s.json 合并为同一个5s更新事件。
  std::map<double, TopologyTimeSlice> slicesByTime;
  DIR* dir = opendir(dirname.c_str());
  if (dir == nullptr)
  {
    NS_FATAL_ERROR("无法打开拓扑目录: " << dirname);
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr)
  {
    std::string filename = entry->d_name;
    if (!EndsWith(filename, ".json") || filename == "time_slices.json")
    {
      continue;
    }

    double seconds = 0.0;
    if (!TryParseSecondsFromTimeSliceFilename(filename, seconds))
    {
      continue;
    }

    if (seconds <= 0.0)
    {
      // 0s文件已经在初始化阶段读取，不能再调度一次运行期更新。
      continue;
    }

    TopologyTimeSlice& slice = slicesByTime[seconds];
    slice.time_s = seconds;
    std::string fullPath = JoinPath(dirname, filename);
    slice.is_patch = patchMode;
    if (patchMode)
    {
      if (StartsWith(filename, "patch_"))
      {
        slice.patch_file = fullPath;
      }
    }
    else if (StartsWith(filename, "nodes_"))
    {
      slice.nodes_file = fullPath;
      slice.has_nodes_update = true;
    }
    else if (StartsWith(filename, "topology_"))
    {
      slice.links_file = fullPath;
      slice.has_links_update = true;
    }
  }
  closedir(dir);

  std::vector<TopologyTimeSlice> slices;
  for (const auto& item : slicesByTime)
  {
    const TopologyTimeSlice& slice = item.second;
    if (slice.is_patch && slice.patch_file.empty())
    {
      continue;
    }
    if (!slice.is_patch && !slice.has_nodes_update && !slice.has_links_update)
    {
      continue;
    }
    slices.push_back(slice);
  }
  return slices;
}

LinkOutputTimeWindow
ScanLinkOutputSnapshotsDirectory(const std::string& dirname,
                                 double simulationDuration)
{
  if (!std::isfinite(simulationDuration) || simulationDuration <= 0.0)
  {
    NS_FATAL_ERROR("simulationDuration必须是正数");
  }

  std::map<int64_t, std::string> filesByTimestamp;
  DIR* dir = opendir(dirname.c_str());
  if (dir == nullptr)
  {
    NS_FATAL_ERROR("无法打开link_output目录: " << dirname);
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr)
  {
    std::string filename = entry->d_name;
    if (!EndsWith(filename, ".json"))
    {
      continue;
    }

    std::string stem = filename.substr(0, filename.size() - 5);
    int64_t timestamp = 0;
    if (!TryParseLinkOutputTimestamp(stem, timestamp))
    {
      if (HasLinkOutputTimestampShape(stem))
      {
        NS_FATAL_ERROR("link_output文件名中的日期或时间无效: " << filename);
      }
      continue;
    }

    std::string fullPath = JoinPath(dirname, filename);
    if (!filesByTimestamp.insert(std::make_pair(timestamp, fullPath)).second)
    {
      NS_FATAL_ERROR("link_output目录存在重复时间戳: " << stem);
    }
  }
  closedir(dir);

  if (filesByTimestamp.empty())
  {
    NS_FATAL_ERROR("link_output目录中没有YYYY-MM-DD_HH-MM-SS.json快照: " << dirname);
  }

  auto initial = filesByTimestamp.begin();
  int64_t startTimestamp = initial->first;
  LinkOutputTimeWindow window;
  window.initial_file = initial->second;
  window.discovered_snapshot_count = filesByTimestamp.size();
  for (auto item = initial; item != filesByTimestamp.end(); ++item)
  {
    double relativeTime = static_cast<double>(item->first - startTimestamp);
    if (relativeTime > simulationDuration)
    {
      break;
    }
    ++window.selected_snapshot_count;
    if (relativeTime == 0.0)
    {
      continue;
    }

    LinkOutputSnapshotFile snapshot;
    snapshot.time_s = relativeTime;
    snapshot.snapshot_file = item->second;
    window.updates.push_back(snapshot);
  }
  return window;
}

} // namespace ns3
