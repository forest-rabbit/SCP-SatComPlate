// 发现并校验按时间命名的节点与链路 JSON 全量快照序列。

#include "snapshot-schedule.h"

#include "ns3/abort.h"

#include <cmath>
#include <dirent.h>
#include <map>
#include <stdexcept>

namespace ns3 {

namespace {

struct SnapshotFiles
{
  std::string nodesFilename;
  std::string linksFilename;
};

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
StartsWith(const std::string& value, const std::string& prefix)
{
  return value.size() >= prefix.size()
         && value.compare(0, prefix.size(), prefix) == 0;
}

bool
EndsWith(const std::string& value, const std::string& suffix)
{
  return value.size() >= suffix.size()
         && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool
TryParseSnapshotSeconds(const std::string& filename,
                        const std::string& prefix,
                        double& seconds)
{
  static const std::string suffix = "s.json";
  if (!StartsWith(filename, prefix)
      || !EndsWith(filename, suffix)
      || filename.size() <= prefix.size() + suffix.size())
    {
      return false;
    }

  std::string value =
    filename.substr(prefix.size(),
                    filename.size() - prefix.size() - suffix.size());
  for (char character : value)
    {
      if ((character < '0' || character > '9') && character != '.')
        {
          return false;
        }
    }

  try
    {
      size_t parsedCharacters = 0;
      seconds = std::stod(value, &parsedCharacters);
      return parsedCharacters == value.size()
             && std::isfinite(seconds)
             && seconds >= 0.0;
    }
  catch (const std::exception&)
    {
      return false;
    }
}

} // namespace

SnapshotSchedule
ScanSatelliteSnapshots(const std::string& directory,
                       double simulationDurationSeconds)
{
  NS_ABORT_MSG_IF(!std::isfinite(simulationDurationSeconds)
                    || simulationDurationSeconds <= 0.0,
                  "simulationDuration 必须是有限正数");

  DIR* handle = opendir(directory.c_str());
  NS_ABORT_MSG_IF(handle == nullptr, "无法打开卫星拓扑目录: " << directory);

  std::map<double, SnapshotFiles> filesByTime;
  struct dirent* entry = nullptr;
  while ((entry = readdir(handle)) != nullptr)
    {
      std::string filename = entry->d_name;
      if (!EndsWith(filename, ".json"))
        {
          continue;
        }
      bool isNodesFile = StartsWith(filename, "nodes_");
      bool isLinksFile = StartsWith(filename, "topology_");
      if (!isNodesFile && !isLinksFile)
        {
          continue;
        }

      double seconds = 0.0;
      const std::string prefix = isNodesFile ? "nodes_" : "topology_";
      NS_ABORT_MSG_IF(!TryParseSnapshotSeconds(filename, prefix, seconds),
                      "无效的拓扑时间片文件名: " << filename);

      SnapshotFiles& files = filesByTime[seconds];
      std::string fullPath = JoinPath(directory, filename);
      if (isNodesFile)
        {
          NS_ABORT_MSG_IF(!files.nodesFilename.empty(),
                          "重复的 nodes 时间片: " << seconds << "s");
          files.nodesFilename = fullPath;
        }
      else
        {
          NS_ABORT_MSG_IF(!files.linksFilename.empty(),
                          "重复的 topology 时间片: " << seconds << "s");
          files.linksFilename = fullPath;
        }
    }
  closedir(handle);

  NS_ABORT_MSG_IF(filesByTime.empty(), "拓扑目录中没有 nodes/topology 快照: "
                                         << directory);
  for (const auto& item : filesByTime)
    {
      NS_ABORT_MSG_IF(item.second.nodesFilename.empty()
                        || item.second.linksFilename.empty(),
                      item.first << "s 必须同时提供 nodes 和 topology 文件");
    }

  auto initial = filesByTime.find(0.0);
  NS_ABORT_MSG_IF(initial == filesByTime.end(),
                  "拓扑目录必须包含 nodes_0s.json 和 topology_0s.json");

  SnapshotSchedule schedule;
  schedule.discoveredSnapshotCount = filesByTime.size();
  schedule.initialNodesFilename = initial->second.nodesFilename;
  schedule.initialLinksFilename = initial->second.linksFilename;

  for (const auto& item : filesByTime)
    {
      if (item.first > simulationDurationSeconds)
        {
          break;
        }
      ++schedule.selectedSnapshotCount;
      if (item.first == 0.0)
        {
          continue;
        }

      SnapshotUpdate update;
      update.timeSeconds = item.first;
      update.nodesFilename = item.second.nodesFilename;
      update.linksFilename = item.second.linksFilename;
      schedule.updates.push_back(update);
    }
  return schedule;
}

} // namespace ns3
