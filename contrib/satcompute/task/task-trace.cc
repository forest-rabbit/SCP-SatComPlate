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

// 解析并校验 traffic 侧的任务到达与数据、计算工作量输入。

#include "task-trace.h"

#include "../jsontopo/json.hpp"
#include "../topo.h"

#include "ns3/abort.h"
#include "ns3/fatal-error.h"
#include "ns3/nstime.h"

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
                  "TaskTrace JSON 缺少字段 " << field << ": " << filename);
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
                  "TaskTrace JSON 缺少字段 " << field << ": " << filename);
  NS_ABORT_MSG_IF(!value->is_string(),
                  field << " 必须是字符串: " << filename);
  return value->get<std::string>();
}

json
ReadJsonFile(const std::string& filename)
{
  std::ifstream input(filename);
  NS_ABORT_MSG_IF(!input.is_open(),
                  "无法打开 TaskTrace JSON: " << filename);

  try
    {
      json root;
      input >> root;
      return root;
    }
  catch (const std::exception& error)
    {
      NS_FATAL_ERROR("TaskTrace JSON 解析失败: " << error.what()
                                                << "\nfile: " << filename);
    }
  return json();
}

uint64_t
CheckedAdd(uint64_t left,
           uint64_t right,
           const std::string& field,
           const std::string& filename)
{
  NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                  "TaskTrace " << field << " 累计溢出: " << filename);
  return left + right;
}

void
PrintTask(const TaskDefinition& task)
{
  std::cout << "  task " << task.taskId
            << " source=" << task.sourceNodeId
            << " compute=" << task.computeNodeId
            << " result=" << task.resultNodeId
            << " input=" << task.inputBytes
            << " output=" << task.outputBytes
            << " work=" << task.computeWorkUnits
            << " arrival_ns=" << task.arrivalTimeNs
            << std::endl;
}

void
LogTaskTrace(const std::string& filename,
             const TaskTrace& trace,
             const std::string& logMode)
{
  if (logMode == "silent")
    {
      return;
    }

  uint64_t totalInputBytes = 0;
  uint64_t totalOutputBytes = 0;
  uint64_t totalWorkUnits = 0;
  uint64_t minimumWork = trace.tasks.front().computeWorkUnits;
  uint64_t maximumWork = minimumWork;
  int64_t firstArrival = trace.tasks.front().arrivalTimeNs;
  int64_t lastArrival = firstArrival;
  for (const auto& task : trace.tasks)
    {
      totalInputBytes =
        CheckedAdd(totalInputBytes, task.inputBytes, "input_bytes", filename);
      totalOutputBytes =
        CheckedAdd(totalOutputBytes, task.outputBytes, "output_bytes", filename);
      totalWorkUnits =
        CheckedAdd(totalWorkUnits,
                   task.computeWorkUnits,
                   "compute_work_units",
                   filename);
      minimumWork = std::min(minimumWork, task.computeWorkUnits);
      maximumWork = std::max(maximumWork, task.computeWorkUnits);
      firstArrival = std::min(firstArrival, task.arrivalTimeNs);
      lastArrival = std::max(lastArrival, task.arrivalTimeNs);
    }

  std::cout << "[TASK:TRACE]" << std::endl
            << "  trace path       : " << filename << std::endl
            << "  tasks            : " << trace.tasks.size() << std::endl
            << "  total input bytes: " << totalInputBytes << std::endl
            << "  total output bytes: " << totalOutputBytes << std::endl
            << "  total work units : " << totalWorkUnits << std::endl
            << "  work min/mean/max: " << minimumWork << "/"
            << static_cast<double>(
                 static_cast<long double>(totalWorkUnits)
                 / trace.tasks.size())
            << "/" << maximumWork << std::endl
            << "  first/last arrival_ns: " << firstArrival << "/"
            << lastArrival << std::endl;

  if (logMode == "verbose" || trace.tasks.size() <= 6)
    {
      for (const auto& task : trace.tasks)
        {
          PrintTask(task);
        }
    }
  else
    {
      for (uint32_t index = 0; index < 3; ++index)
        {
          PrintTask(trace.tasks[index]);
        }
      std::cout << "  ..." << std::endl;
      for (uint32_t index = trace.tasks.size() - 3;
           index < trace.tasks.size();
           ++index)
        {
          PrintTask(trace.tasks[index]);
        }
    }
  std::cout << std::endl;
}

} // namespace

TaskTrace
ReadTaskTrace(const std::string& filename,
              double simulationDurationSeconds,
              const SatelliteTopology& topology,
              const ComputeProfile& computeProfile,
              const std::string& logMode)
{
  NS_ABORT_MSG_IF(filename.empty(), "TaskTrace JSON 路径不能为空");
  NS_ABORT_MSG_IF(logMode != "summary"
                    && logMode != "verbose"
                    && logMode != "silent",
                  "taskLogMode 必须是 summary、verbose 或 silent");

  int64_t simulationDurationNs =
    Seconds(simulationDurationSeconds).GetNanoSeconds();
  NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                  "simulationDuration 无法表示为正的纳秒时长");

  json root = ReadJsonFile(filename);
  NS_ABORT_MSG_IF(!root.is_object(),
                  "TaskTrace JSON 根节点必须是对象: " << filename);
  NS_ABORT_MSG_IF(
    root.size() != 2,
    "TaskTrace JSON 根节点只允许 schema_version 和 tasks: " << filename);
  NS_ABORT_MSG_IF(GetRequiredString(root, "schema_version", filename) != "0.1",
                  "TaskTrace schema_version 必须严格等于 0.1: " << filename);

  const json* items = FindField(root, "tasks");
  NS_ABORT_MSG_IF(items == nullptr || !items->is_array(),
                  "TaskTrace JSON 缺少 tasks 数组: " << filename);
  NS_ABORT_MSG_IF(items->empty(),
                  "TaskTrace 至少需要一个任务: " << filename);

  TaskTrace trace;
  std::set<uint64_t> taskIds;
  for (const auto& item : *items)
    {
      NS_ABORT_MSG_IF(!item.is_object(),
                      "tasks 数组元素必须是对象: " << filename);
      NS_ABORT_MSG_IF(
        item.size() != 8,
        "task 只允许 task_id、source_node_id、compute_node_id、"
        "result_node_id、input_bytes、output_bytes、compute_work_units "
        "和 arrival_time_ns: " << filename);

      TaskDefinition task;
      task.taskId = GetRequiredUint64(item, "task_id", filename);
      NS_ABORT_MSG_IF(
        task.taskId == 0
          || task.taskId > std::numeric_limits<uint64_t>::max() / 2,
        "task_id 必须为正且不超过 UINT64_MAX/2: " << filename);
      NS_ABORT_MSG_IF(!taskIds.insert(task.taskId).second,
                      "TaskTrace 包含重复 task_id: " << task.taskId);

      task.sourceNodeId =
        GetRequiredUint32(item, "source_node_id", filename);
      task.computeNodeId =
        GetRequiredUint32(item, "compute_node_id", filename);
      task.resultNodeId =
        GetRequiredUint32(item, "result_node_id", filename);
      NS_ABORT_MSG_IF(!topology.HasSatelliteId(task.sourceNodeId),
                      "TaskTrace 引用了未知 source_node_id="
                        << task.sourceNodeId << "，task_id=" << task.taskId);
      NS_ABORT_MSG_IF(!topology.HasSatelliteId(task.computeNodeId),
                      "TaskTrace 引用了未知 compute_node_id="
                        << task.computeNodeId << "，task_id=" << task.taskId);
      NS_ABORT_MSG_IF(!topology.HasSatelliteId(task.resultNodeId),
                      "TaskTrace 引用了未知 result_node_id="
                        << task.resultNodeId << "，task_id=" << task.taskId);
      NS_ABORT_MSG_IF(
        FindComputeNodeProfile(computeProfile, task.computeNodeId) == nullptr,
        "compute_node_id 必须引用 ComputeProfile，task_id="
          << task.taskId << " compute_node_id=" << task.computeNodeId);
      NS_ABORT_MSG_IF(task.sourceNodeId == task.computeNodeId,
                      "source_node_id 不能等于 compute_node_id，task_id="
                        << task.taskId);
      NS_ABORT_MSG_IF(task.computeNodeId == task.resultNodeId,
                      "compute_node_id 不能等于 result_node_id，task_id="
                        << task.taskId);

      task.inputBytes = GetRequiredUint64(item, "input_bytes", filename);
      task.outputBytes = GetRequiredUint64(item, "output_bytes", filename);
      task.computeWorkUnits =
        GetRequiredUint64(item, "compute_work_units", filename);
      NS_ABORT_MSG_IF(task.inputBytes == 0,
                      "input_bytes 必须为正整数，task_id=" << task.taskId);
      NS_ABORT_MSG_IF(task.outputBytes == 0,
                      "output_bytes 必须为正整数，task_id=" << task.taskId);
      NS_ABORT_MSG_IF(task.computeWorkUnits == 0,
                      "compute_work_units 必须为正整数，task_id="
                        << task.taskId);

      uint64_t arrivalTimeNs =
        GetRequiredUint64(item, "arrival_time_ns", filename);
      NS_ABORT_MSG_IF(
        arrivalTimeNs
          > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
        "arrival_time_ns 超出 int64 范围，task_id=" << task.taskId);
      task.arrivalTimeNs = static_cast<int64_t>(arrivalTimeNs);
      NS_ABORT_MSG_IF(
        task.arrivalTimeNs >= simulationDurationNs,
        "arrival_time_ns 必须早于 simulation stop，task_id=" << task.taskId);

      task.inputTransferId = task.taskId * 2 - 1;
      task.resultTransferId = task.taskId * 2;
      trace.tasks.push_back(task);
    }

  std::sort(trace.tasks.begin(),
            trace.tasks.end(),
            [](const TaskDefinition& left, const TaskDefinition& right) {
              return left.taskId < right.taskId;
            });
  LogTaskTrace(filename, trace, logMode);
  return trace;
}

} // namespace ns3
