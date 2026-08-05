/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "snapshot-schedule.h"

#include "snapshot-reader.h"
#include "../../time-conversion.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace ns3
{

namespace
{

struct SnapshotFiles
{
    std::filesystem::path nodesFilename;
    std::filesystem::path linksFilename;
};

bool
HasPrefixAndSuffix(std::string_view value,
                   std::string_view prefix,
                   std::string_view suffix)
{
    return value.size() > prefix.size() + suffix.size() && value.starts_with(prefix) &&
           value.ends_with(suffix);
}

int64_t
ParseSnapshotTimeNs(const std::string& filename, std::string_view prefix)
{
    constexpr std::string_view suffix = "s.json";
    const std::string_view name(filename);
    const std::string_view token =
        name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());

    bool hasDot = false;
    for (const char character : token)
    {
        if (character == '.')
        {
            if (hasDot)
            {
                throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
            }
            hasDot = true;
            continue;
        }
        if (character < '0' || character > '9')
        {
            throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
        }
    }
    if (token.empty() || token.front() == '.' || token.back() == '.')
    {
        throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
    }

    try
    {
        return SatComputeDecimalSecondsToNanoseconds(token, "snapshot filename time");
    }
    catch (const SatComputeTimeError& error)
    {
        throw TopologySnapshotError("invalid snapshot timestamp filename " + filename +
                                    " (" + error.what() + ")");
    }
}

std::string
FormatTimeNs(int64_t timeNs)
{
    std::ostringstream value;
    value << timeNs << " ns";
    return value.str();
}

} // namespace

SnapshotSchedule
ScanSatelliteSnapshots(const std::filesystem::path& directory,
                       int64_t simulationDurationNs,
                       int64_t networkUpdateIntervalNs)
{
    if (simulationDurationNs <= 0)
    {
        throw TopologySnapshotError("simulation duration must be positive");
    }
    if (networkUpdateIntervalNs <= 0)
    {
        throw TopologySnapshotError("network update interval must be positive");
    }

    std::map<int64_t, SnapshotFiles> filesByTime;
    try
    {
        if (!std::filesystem::is_directory(directory))
        {
            throw TopologySnapshotError("cannot open satellite topology directory: " +
                                        directory.string());
        }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            constexpr std::string_view suffix = "s.json";
            constexpr std::string_view nodesPrefix = "nodes_";
            constexpr std::string_view linksPrefix = "topology_";
            const bool isNodesFile =
                filename.starts_with(nodesPrefix) && filename.ends_with(".json");
            const bool isLinksFile =
                filename.starts_with(linksPrefix) && filename.ends_with(".json");
            if (!isNodesFile && !isLinksFile)
            {
                continue;
            }

            const std::string_view prefix = isNodesFile ? nodesPrefix : linksPrefix;
            if (!HasPrefixAndSuffix(filename, prefix, suffix))
            {
                throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
            }
            const int64_t timeNs = ParseSnapshotTimeNs(filename, prefix);
            SnapshotFiles& files = filesByTime[timeNs];
            std::filesystem::path& selected =
                isNodesFile ? files.nodesFilename : files.linksFilename;
            if (!selected.empty())
            {
                throw TopologySnapshotError("duplicate canonical snapshot time " +
                                            FormatTimeNs(timeNs) + ": " + filename);
            }
            selected = entry.path().lexically_normal();
        }
    }
    catch (const TopologySnapshotError&)
    {
        throw;
    }
    catch (const std::filesystem::filesystem_error& error)
    {
        throw TopologySnapshotError("cannot scan satellite topology directory " +
                                    directory.string() + " (" + error.what() + ")");
    }

    if (filesByTime.empty())
    {
        throw TopologySnapshotError("topology directory contains no snapshot pairs: " +
                                    directory.string());
    }
    if (filesByTime.size() > std::numeric_limits<uint32_t>::max())
    {
        throw TopologySnapshotError("topology directory contains too many snapshots");
    }
    for (const auto& [timeNs, files] : filesByTime)
    {
        if (files.nodesFilename.empty() || files.linksFilename.empty())
        {
            throw TopologySnapshotError(FormatTimeNs(timeNs) +
                                        " must provide both nodes and topology files");
        }
    }

    SnapshotSchedule schedule;
    schedule.manifestAuthoritative = false;
    schedule.discoveredSnapshotCount = static_cast<uint32_t>(filesByTime.size());
    for (int64_t timeNs = 0; timeNs < simulationDurationNs;)
    {
        const auto files = filesByTime.find(timeNs);
        if (files == filesByTime.end())
        {
            throw TopologySnapshotError("missing replay snapshot at configured update time " +
                                        FormatTimeNs(timeNs));
        }

        ++schedule.selectedSnapshotCount;
        if (timeNs == 0)
        {
            schedule.initialNodesFilename = files->second.nodesFilename;
            schedule.initialLinksFilename = files->second.linksFilename;
        }
        else
        {
            schedule.updates.push_back(
                {timeNs, files->second.nodesFilename, files->second.linksFilename});
        }

        if (simulationDurationNs - timeNs <= networkUpdateIntervalNs)
        {
            break;
        }
        timeNs += networkUpdateIntervalNs;
    }
    return schedule;
}

} // namespace ns3
