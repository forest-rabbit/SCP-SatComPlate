#include "topo-json.h"
#include "../sdn-controller/json.hpp"
#include "ns3/fatal-error.h"
#include "ns3/log.h"

#include <algorithm>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <map>
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
  file >> data;
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
                      bool zeroBasedFallback)
{
  uint32_t node1 = GetRequiredUint32Field(item, {"node1_id", "nodel_id", "node1 id", "source", "src"});
  uint32_t node2 = GetRequiredUint32Field(item, {"node2_id", "node2 id", "destination", "dst", "dest"});

  LinkInfo link;
  // JSON中的节点ID可以是外部node_id，也可以在没有nodes文件时退化为0/1基下标。
  // resolver由topo.cc提供，因此解析层不依赖NodeContainer。
  link.source = resolver(node1, zeroBasedFallback);
  link.destination = resolver(node2, zeroBasedFallback);
  link.type = GetStringField(item, {"type", "link_type"}, "sat");
  link.delay_us = GetUint32Field(item, {"delay", "delay_us"}, 0);
  link.has_delay_us = FindJsonField(item, {"delay", "delay_us"}) != nullptr;
  link.delay_ms = link.has_delay_us ? (link.delay_us + 999) / 1000 : GetUint32Field(item, {"delay_ms"}, 0);
  link.link_bandwidth_kbps = GetUint32Field(item, {"link_bandwidth", "link_bandwidth_kbps"}, 0);
  link.has_link_bandwidth_kbps = FindJsonField(item, {"link_bandwidth", "link_bandwidth_kbps"}) != nullptr;
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
                            bool hasExplicitNodeIds)
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
    links.push_back(ParseTopologyLinkJson(item, resolver, zeroBasedFallback));
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

std::vector<TopologyTimeSlice>
ReadTopologyTimeSlicesJsonFile(const std::string& filename,
                               const TopologyNodeResolver& resolver,
                               bool hasExplicitNodeIds)
{
  json root = ReadJsonFile(filename);
  json slicesArray = ExtractJsonArray(root, {"time_slices", "timeslices", "slices", "data"});
  std::vector<TopologyTimeSlice> slices;
  for (const auto& item : slicesArray)
  {
    TopologyTimeSlice slice;
    slice.nodes_file = GetStringField(item, {"nodes_file", "node_file"}, "");
    slice.nodes_file = ResolveRelativePath(filename, slice.nodes_file);
    slice.links_file = GetStringField(item, {"links_file", "topology_file", "file"}, "");
    slice.links_file = ResolveRelativePath(filename, slice.links_file);
    if (FindJsonField(item, {"time", "time_s", "sim_time"}) != nullptr)
    {
      slice.time_s = GetDoubleField(item, {"time", "time_s", "sim_time"}, 0.0);
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

    auto nodesIt = item.find("nodes");
    if (nodesIt != item.end() && nodesIt->is_array())
    {
      for (const auto& nodeItem : *nodesIt)
      {
        slice.nodes.push_back(ParseTopologyNodeJson(nodeItem));
      }
    }

    auto linksIt = item.find("links");
    if (linksIt != item.end() && linksIt->is_array())
    {
      slice.links = ParseTopologyLinksJsonArray(*linksIt, resolver, hasExplicitNodeIds);
    }

    slices.push_back(slice);
  }
  std::sort(slices.begin(), slices.end(), [](const TopologyTimeSlice& lhs, const TopologyTimeSlice& rhs) {
    return lhs.time_s < rhs.time_s;
  });
  return slices;
}

std::vector<TopologyTimeSlice>
ScanTopologyTimeSlicesDirectory(const std::string& dirname)
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
    if (StartsWith(filename, "nodes_"))
    {
      slice.nodes_file = fullPath;
    }
    else if (StartsWith(filename, "topology_"))
    {
      slice.links_file = fullPath;
    }
  }
  closedir(dir);

  std::vector<TopologyTimeSlice> slices;
  for (const auto& item : slicesByTime)
  {
    const TopologyTimeSlice& slice = item.second;
    if (slice.nodes_file.empty() && slice.links_file.empty())
    {
      continue;
    }
    slices.push_back(slice);
  }
  return slices;
}

} // namespace ns3
