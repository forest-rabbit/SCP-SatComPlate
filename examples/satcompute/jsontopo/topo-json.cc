#include "topo-json.h"

#include "json.hpp"
#include "ns3/abort.h"
#include "ns3/fatal-error.h"

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

namespace {

std::string
JoinPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory.back() == '/')
    {
      return directory + filename;
    }
  return directory + "/" + filename;
}

bool
EndsWith(const std::string& value, const std::string& suffix)
{
  return value.size() >= suffix.size()
         && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const json*
FindField(const json& item, const std::string& name)
{
  json::const_iterator field = item.find(name);
  return field == item.end() ? nullptr : &(*field);
}

uint32_t
ParseUint32(const json& value, const std::string& field, const std::string& filename)
{
  try
    {
      if (value.is_number_unsigned())
        {
          uint64_t parsed = value.get<uint64_t>();
          NS_ABORT_MSG_IF(parsed > std::numeric_limits<uint32_t>::max(),
                          field << " 超出 uint32 范围: " << filename);
          return static_cast<uint32_t>(parsed);
        }
      if (value.is_number_integer())
        {
          int64_t parsed = value.get<int64_t>();
          NS_ABORT_MSG_IF(parsed < 0
                            || static_cast<uint64_t>(parsed)
                                 > std::numeric_limits<uint32_t>::max(),
                          field << " 必须是非负 uint32: " << filename);
          return static_cast<uint32_t>(parsed);
        }
    }
  catch (const std::exception& error)
    {
      NS_FATAL_ERROR(field << " 解析失败: " << error.what() << "\nfile: " << filename);
    }

  NS_FATAL_ERROR(field << " 必须是非负整数: " << filename);
  return 0;
}

uint32_t
GetRequiredUint32(const json& item, const std::string& field, const std::string& filename)
{
  const json* value = FindField(item, field);
  NS_ABORT_MSG_IF(value == nullptr, "JSON 缺少字段 " << field << ": " << filename);
  return ParseUint32(*value, field, filename);
}

double
GetRequiredDouble(const json& item, const std::string& field, const std::string& filename)
{
  const json* value = FindField(item, field);
  NS_ABORT_MSG_IF(value == nullptr, "JSON 缺少字段 " << field << ": " << filename);
  NS_ABORT_MSG_IF(!value->is_number(), field << " 必须是数值: " << filename);

  double parsed = value->get<double>();
  NS_ABORT_MSG_IF(!std::isfinite(parsed) || parsed < 0.0,
                  field << " 必须是有限非负数: " << filename);
  return parsed;
}

uint64_t
GetOptionalBandwidthBps(const json& item, const std::string& filename)
{
  const json* value = FindField(item, "bandwidth_bps");
  if (value == nullptr)
    {
      return 0;
    }

  NS_ABORT_MSG_IF(!value->is_number_unsigned() && !value->is_number_integer(),
                  "链路带宽必须是非负整数: " << filename);
  if (value->is_number_unsigned())
    {
      uint64_t parsed = value->get<uint64_t>();
      NS_ABORT_MSG_IF(parsed == 0, "链路带宽必须大于 0: " << filename);
      return parsed;
    }

  int64_t parsed = value->get<int64_t>();
  NS_ABORT_MSG_IF(parsed <= 0, "链路带宽必须大于 0: " << filename);
  return static_cast<uint64_t>(parsed);
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
      NS_FATAL_ERROR("JSON 解析失败: " << error.what() << "\nfile: " << filename);
    }
  return json();
}

bool
HasTimestampShape(const std::string& value)
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

uint32_t
ParseTimestampNumber(const std::string& value, uint32_t position, uint32_t length)
{
  uint32_t result = 0;
  for (uint32_t i = 0; i < length; ++i)
    {
      result = result * 10 + static_cast<uint32_t>(value[position + i] - '0');
    }
  return result;
}

bool
IsLeapYear(uint32_t year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

} // namespace

SatelliteSnapshot
ReadSatelliteSnapshot(const std::string& filename)
{
  json root = ReadJsonFile(filename);
  NS_ABORT_MSG_IF(!root.is_array(), "拓扑快照根节点必须是数组: " << filename);

  SatelliteSnapshot snapshot;
  std::set<uint32_t> satelliteIds;
  std::set<uint32_t> satelliteRecordIds;
  std::set<std::pair<uint32_t, uint32_t>> linkKeys;

  for (const auto& item : root)
    {
      NS_ABORT_MSG_IF(!item.is_object(), "拓扑快照数组元素必须是对象: " << filename);

      const bool hasFirstEndpoint = FindField(item, "node1_id") != nullptr;
      const bool hasSecondEndpoint = FindField(item, "node2_id") != nullptr;
      if (hasFirstEndpoint || hasSecondEndpoint)
        {
          uint32_t sourceId = GetRequiredUint32(item, "node1_id", filename);
          uint32_t destinationId = GetRequiredUint32(item, "node2_id", filename);
          NS_ABORT_MSG_IF(sourceId == destinationId,
                          "卫星链路不能连接节点自身: " << sourceId << "\nfile: " << filename);

          double delayMs = GetRequiredDouble(item, "delay", filename);
          long double delayUs = static_cast<long double>(delayMs) * 1000.0L;
          NS_ABORT_MSG_IF(delayUs > std::numeric_limits<int64_t>::max(),
                          "delay 超出可表示范围: " << filename);

          SatelliteLink link;
          link.sourceId = sourceId;
          link.destinationId = destinationId;
          link.delayUs = static_cast<uint64_t>(std::llround(delayUs));
          link.bandwidthBps = GetOptionalBandwidthBps(item, filename);

          std::pair<uint32_t, uint32_t> key =
            sourceId < destinationId
              ? std::make_pair(sourceId, destinationId)
              : std::make_pair(destinationId, sourceId);
          NS_ABORT_MSG_IF(!linkKeys.insert(key).second,
                          "拓扑快照包含重复卫星链路: " << key.first << "<->" << key.second
                          << "\nfile: " << filename);

          snapshot.links.push_back(link);
          satelliteIds.insert(sourceId);
          satelliteIds.insert(destinationId);
          continue;
        }

      const json* satelliteId = FindField(item, "sat_id");
      NS_ABORT_MSG_IF(satelliteId == nullptr,
                      "无法识别的拓扑快照对象；需要 sat_id 或 node1_id/node2_id: "
                        << filename);
      uint32_t parsedId = ParseUint32(*satelliteId, "sat_id", filename);
      NS_ABORT_MSG_IF(!satelliteRecordIds.insert(parsedId).second,
                      "拓扑快照包含重复 sat_id: " << parsedId
                      << "\nfile: " << filename);
      satelliteIds.insert(parsedId);
    }

  NS_ABORT_MSG_IF(satelliteRecordIds != satelliteIds,
                  "每个 ISL 端点都必须有对应的 sat_id 记录: " << filename);
  snapshot.satelliteIds.assign(satelliteIds.begin(), satelliteIds.end());
  NS_ABORT_MSG_IF(snapshot.satelliteIds.empty(), "拓扑快照中没有卫星: " << filename);
  return snapshot;
}

bool
TryParseSnapshotTimestamp(const std::string& value, int64_t& timestampSeconds)
{
  if (!HasTimestampShape(value))
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

SnapshotSchedule
ScanSatelliteSnapshots(const std::string& directory, double simulationDurationSeconds)
{
  NS_ABORT_MSG_IF(!std::isfinite(simulationDurationSeconds)
                    || simulationDurationSeconds <= 0.0,
                  "simulationDuration 必须是有限正数");

  DIR* handle = opendir(directory.c_str());
  NS_ABORT_MSG_IF(handle == nullptr, "无法打开卫星拓扑目录: " << directory);

  std::map<int64_t, std::string> filesByTimestamp;
  struct dirent* entry = nullptr;
  while ((entry = readdir(handle)) != nullptr)
    {
      std::string filename = entry->d_name;
      if (!EndsWith(filename, ".json"))
        {
          continue;
        }

      std::string stem = filename.substr(0, filename.size() - 5);
      int64_t timestamp = 0;
      if (!TryParseSnapshotTimestamp(stem, timestamp))
        {
          NS_ABORT_MSG_IF(HasTimestampShape(stem),
                          "拓扑快照文件名包含无效日期或时间: " << filename);
          continue;
        }

      bool inserted =
        filesByTimestamp.insert(std::make_pair(timestamp, JoinPath(directory, filename))).second;
      NS_ABORT_MSG_IF(!inserted, "拓扑目录包含重复时间戳: " << stem);
    }
  closedir(handle);

  NS_ABORT_MSG_IF(filesByTimestamp.empty(),
                  "拓扑目录中没有 YYYY-MM-DD_HH-MM-SS.json 快照: " << directory);

  SnapshotSchedule schedule;
  schedule.discoveredSnapshotCount = filesByTimestamp.size();
  const int64_t firstTimestamp = filesByTimestamp.begin()->first;
  schedule.initialFilename = filesByTimestamp.begin()->second;

  for (const auto& item : filesByTimestamp)
    {
      double relativeTime = static_cast<double>(item.first - firstTimestamp);
      if (relativeTime > simulationDurationSeconds)
        {
          break;
        }
      ++schedule.selectedSnapshotCount;
      if (relativeTime > 0.0)
        {
          SnapshotUpdate update;
          update.timeSeconds = relativeTime;
          update.filename = item.second;
          schedule.updates.push_back(update);
        }
    }
  return schedule;
}

} // namespace ns3
