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

// 解析并校验 topology 侧的静态计算节点能力输入。

#include "compute-profile.h"

#include "../jsontopo/json.hpp"
#include "../topo.h"

#include "ns3/abort.h"
#include "ns3/fatal-error.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>

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
  NS_ABORT_MSG_IF(value == nullptr,
                  "ComputeProfile JSON 缺少字段 " << field
                    << ": " << filename);
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
  NS_ABORT_MSG_IF(value == nullptr,
                  "ComputeProfile JSON 缺少字段 " << field
                    << ": " << filename);
  NS_ABORT_MSG_IF(!value->is_string(),
                  field << " 必须是字符串: " << filename);
  return value->get<std::string>();
}

json
ReadJsonFile(const std::string& filename)
{
  std::ifstream input(filename);
  NS_ABORT_MSG_IF(!input.is_open(),
                  "无法打开 ComputeProfile JSON: " << filename);

  try
    {
      json root;
      input >> root;
      return root;
    }
  catch (const std::exception& error)
    {
      NS_FATAL_ERROR("ComputeProfile JSON 解析失败: " << error.what()
                                                    << "\nfile: " << filename);
    }
  return json();
}

void
LogComputeProfile(const std::string& filename,
                  const ComputeProfile& profile,
                  const std::string& logMode)
{
  if (logMode == "silent")
    {
      return;
    }

  uint64_t minimumRate = profile.nodes.front().computeRateWorkUnitsPerSecond;
  uint64_t maximumRate = minimumRate;
  long double totalRate = 0.0L;
  for (const auto& node : profile.nodes)
    {
      minimumRate =
        std::min(minimumRate, node.computeRateWorkUnitsPerSecond);
      maximumRate =
        std::max(maximumRate, node.computeRateWorkUnitsPerSecond);
      totalRate +=
        static_cast<long double>(node.computeRateWorkUnitsPerSecond);
    }

  std::cout << "[COMPUTE:PROFILE]" << std::endl
            << "  profile path : " << filename << std::endl
            << "  compute nodes: " << profile.nodes.size() << std::endl
            << "  rate min/mean/max: " << minimumRate << "/"
            << static_cast<double>(totalRate / profile.nodes.size()) << "/"
            << maximumRate << std::endl;
  if (logMode == "verbose")
    {
      for (const auto& node : profile.nodes)
        {
          std::cout << "  node " << node.nodeId
                    << " rate=" << node.computeRateWorkUnitsPerSecond
                    << std::endl;
        }
    }
  else
    {
      const ComputeNodeProfile& first = profile.nodes.front();
      const ComputeNodeProfile& last = profile.nodes.back();
      std::cout << "  first node   : " << first.nodeId
                << " rate=" << first.computeRateWorkUnitsPerSecond
                << std::endl
                << "  last node    : " << last.nodeId
                << " rate=" << last.computeRateWorkUnitsPerSecond
                << std::endl;
    }
  std::cout << std::endl;
}

} // namespace

ComputeProfile
ReadComputeProfile(const std::string& filename,
                   const SatelliteTopology& topology,
                   const std::string& logMode)
{
  NS_ABORT_MSG_IF(filename.empty(), "ComputeProfile JSON 路径不能为空");
  NS_ABORT_MSG_IF(logMode != "summary"
                    && logMode != "verbose"
                    && logMode != "silent",
                  "taskLogMode 必须是 summary、verbose 或 silent");

  json root = ReadJsonFile(filename);
  NS_ABORT_MSG_IF(!root.is_object(),
                  "ComputeProfile JSON 根节点必须是对象: " << filename);
  NS_ABORT_MSG_IF(
    root.size() != 2,
    "ComputeProfile JSON 根节点只允许 schema_version 和 compute_nodes: "
      << filename);
  NS_ABORT_MSG_IF(
    GetRequiredString(root, "schema_version", filename) != "0.1",
    "ComputeProfile schema_version 必须严格等于 0.1: " << filename);

  const json* items = FindField(root, "compute_nodes");
  NS_ABORT_MSG_IF(items == nullptr || !items->is_array(),
                  "ComputeProfile JSON 缺少 compute_nodes 数组: "
                    << filename);
  NS_ABORT_MSG_IF(items->empty(),
                  "ComputeProfile 至少需要一个计算节点: " << filename);

  ComputeProfile profile;
  std::set<uint32_t> nodeIds;
  for (const auto& item : *items)
    {
      NS_ABORT_MSG_IF(!item.is_object(),
                      "compute_nodes 数组元素必须是对象: " << filename);
      NS_ABORT_MSG_IF(
        item.size() != 2,
        "compute node 只允许 node_id 和 "
        "compute_rate_work_units_per_second: " << filename);

      ComputeNodeProfile node;
      node.nodeId = GetRequiredUint32(item, "node_id", filename);
      NS_ABORT_MSG_IF(!topology.HasSatelliteId(node.nodeId),
                      "ComputeProfile 引用了未知卫星 " << node.nodeId);
      NS_ABORT_MSG_IF(!nodeIds.insert(node.nodeId).second,
                      "ComputeProfile 包含重复 node_id: " << node.nodeId);
      node.computeRateWorkUnitsPerSecond =
        GetRequiredUint64(item,
                          "compute_rate_work_units_per_second",
                          filename);
      NS_ABORT_MSG_IF(node.computeRateWorkUnitsPerSecond == 0,
                      "compute_rate_work_units_per_second 必须为正整数，node_id="
                        << node.nodeId);
      profile.nodes.push_back(node);
    }

  std::sort(profile.nodes.begin(),
            profile.nodes.end(),
            [](const ComputeNodeProfile& left,
               const ComputeNodeProfile& right) {
              return left.nodeId < right.nodeId;
            });
  LogComputeProfile(filename, profile, logMode);
  return profile;
}

const ComputeNodeProfile*
FindComputeNodeProfile(const ComputeProfile& profile, uint32_t nodeId)
{
  auto node =
    std::lower_bound(profile.nodes.begin(),
                     profile.nodes.end(),
                     nodeId,
                     [](const ComputeNodeProfile& candidate, uint32_t id) {
                       return candidate.nodeId < id;
                     });
  return node != profile.nodes.end() && node->nodeId == nodeId
           ? &(*node)
           : nullptr;
}

const ComputeNodeProfile&
GetComputeNodeProfile(const ComputeProfile& profile, uint32_t nodeId)
{
  const ComputeNodeProfile* node = FindComputeNodeProfile(profile, nodeId);
  NS_ABORT_MSG_IF(node == nullptr,
                  "ComputeProfile 中不存在 node_id=" << nodeId);
  return *node;
}

} // namespace ns3
