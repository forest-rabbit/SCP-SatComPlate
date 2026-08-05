/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "snapshot-schedule.h"

#include "snapshot-reader.h"
#include "../../para.h"
#include "../../sha256.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
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

using Json = nlohmann::json;

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

    bool previousWasDot = false;
    bool hasDot = false;
    for (const char character : token)
    {
        if (character == '.')
        {
            if (hasDot || previousWasDot)
            {
                throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
            }
            hasDot = true;
            previousWasDot = true;
            continue;
        }
        if (character < '0' || character > '9')
        {
            throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
        }
        previousWasDot = false;
    }
    if (token.empty() || token.front() == '.' || token.back() == '.')
    {
        throw TopologySnapshotError("invalid snapshot timestamp filename: " + filename);
    }

    try
    {
        return SatComputeDecimalSecondsToNanoseconds(token, "snapshot filename time");
    }
    catch (const SatComputeConfigError& error)
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

[[noreturn]] void
FailManifest(const std::filesystem::path& manifestPath, const std::string& message)
{
    throw TopologySnapshotError(message + ": " + manifestPath.string());
}

Json
ReadManifestJson(const std::filesystem::path& manifestPath)
{
    std::ifstream input(manifestPath, std::ios::binary);
    if (!input.is_open())
    {
        FailManifest(manifestPath, "cannot open topology trace manifest");
    }
    try
    {
        Json root;
        input >> root;
        return root;
    }
    catch (const nlohmann::json::exception& error)
    {
        FailManifest(manifestPath,
                     "cannot parse topology trace manifest (" + std::string(error.what()) + ")");
    }
}

void
RequireManifestFields(const Json& value,
                      std::initializer_list<std::string_view> expectedFields,
                      std::string_view context,
                      const std::filesystem::path& manifestPath)
{
    if (!value.is_object())
    {
        FailManifest(manifestPath, std::string(context) + " must be an object");
    }
    std::set<std::string> expected;
    for (const std::string_view field : expectedFields)
    {
        expected.emplace(field);
        if (!value.contains(std::string(field)))
        {
            FailManifest(manifestPath,
                         std::string(context) + " is missing field " + std::string(field));
        }
    }
    for (const auto& item : value.items())
    {
        if (!expected.contains(item.key()))
        {
            FailManifest(manifestPath,
                         std::string(context) + " contains unknown field " + item.key());
        }
    }
}

uint64_t
ParseManifestUint(const Json& value,
                  std::string_view field,
                  const std::filesystem::path& manifestPath)
{
    try
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
    }
    catch (const nlohmann::json::exception& error)
    {
        FailManifest(manifestPath,
                     std::string(field) + " cannot be represented (" + error.what() + ")");
    }
    FailManifest(manifestPath, std::string(field) + " must be a non-negative integer");
}

int64_t
ParseManifestTime(const Json& value,
                  std::string_view field,
                  const std::filesystem::path& manifestPath,
                  bool positive = false)
{
    const uint64_t parsed = ParseManifestUint(value, field, manifestPath);
    if (parsed > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        (positive && parsed == 0))
    {
        FailManifest(manifestPath,
                     std::string(field) + " is outside the supported nanosecond range");
    }
    return static_cast<int64_t>(parsed);
}

std::string
ParseManifestString(const Json& value,
                    std::string_view field,
                    const std::filesystem::path& manifestPath)
{
    if (!value.is_string())
    {
        FailManifest(manifestPath, std::string(field) + " must be a string");
    }
    return value.get<std::string>();
}

void
RequireManifestString(const Json& value,
                      std::string_view field,
                      std::string_view expected,
                      const std::filesystem::path& manifestPath)
{
    if (ParseManifestString(value, field, manifestPath) != expected)
    {
        FailManifest(manifestPath,
                     std::string(field) + " must equal " + std::string(expected));
    }
}

void
RequireSha256(const std::string& digest,
              std::string_view field,
              const std::filesystem::path& manifestPath)
{
    if (digest.size() != 64)
    {
        FailManifest(manifestPath, std::string(field) + " must be a lowercase SHA-256");
    }
    for (const char digit : digest)
    {
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
        {
            FailManifest(manifestPath,
                         std::string(field) + " must be a lowercase SHA-256");
        }
    }
}

std::filesystem::path
ResolveManifestFile(const std::filesystem::path& directory,
                    const std::string& filename,
                    std::string_view expectedPrefix,
                    int64_t expectedTimeNs,
                    const std::filesystem::path& manifestPath)
{
    const std::filesystem::path relative(filename);
    if (relative.empty() || relative.is_absolute() || relative.has_parent_path() ||
        relative.filename() != relative)
    {
        FailManifest(manifestPath, "slice filename must be a manifest-relative basename");
    }
    constexpr std::string_view suffix = "s.json";
    if (!HasPrefixAndSuffix(filename, expectedPrefix, suffix) ||
        ParseSnapshotTimeNs(filename, expectedPrefix) != expectedTimeNs)
    {
        FailManifest(manifestPath,
                     "slice filename time differs from simulation_time_ns: " + filename);
    }
    const std::filesystem::path resolved = (directory / relative).lexically_normal();
    if (!std::filesystem::is_regular_file(resolved))
    {
        FailManifest(manifestPath, "manifest slice file is missing: " + filename);
    }
    return resolved;
}

void
VerifyManifestHash(const std::filesystem::path& file,
                   const std::string& expected,
                   const std::filesystem::path& manifestPath)
{
    RequireSha256(expected, "slice hash", manifestPath);
    try
    {
        if (Sha256File(file) != expected)
        {
            FailManifest(manifestPath,
                         "topology trace SHA-256 differs for " + file.filename().string());
        }
    }
    catch (const TopologySnapshotError&)
    {
        throw;
    }
    catch (const std::exception& error)
    {
        FailManifest(manifestPath,
                     "cannot hash topology trace file " + file.filename().string() + " (" +
                         error.what() + ")");
    }
}

std::map<int64_t, SnapshotFiles>
ReadManifestFiles(const std::filesystem::path& directory,
                  const std::filesystem::path& manifestPath)
{
    const Json root = ReadManifestJson(manifestPath);
    if (!root.is_object() || !root.contains("schema_version"))
    {
        FailManifest(manifestPath, "manifest root is missing field schema_version");
    }
    const std::string schemaVersion =
        ParseManifestString(root.at("schema_version"), "schema_version", manifestPath);
    if (schemaVersion == "0.2")
    {
        RequireManifestFields(root,
                              {"schema_version",
                               "scenario_name",
                               "scenario_config_sha256",
                               "ns3_version",
                               "state_semantics",
                               "coordinate_frame",
                               "coordinate_units",
                               "speed_of_light_m_per_s",
                               "simulation_duration_ns",
                               "trace_interval_ns",
                               "network_update_interval_ns",
                               "include_final_state",
                               "constellation",
                               "topology",
                               "randomness",
                               "slice_count",
                               "slices"},
                              "manifest root",
                              manifestPath);
    }
    else if (schemaVersion == "0.3")
    {
        RequireManifestFields(root,
                              {"schema_version",
                               "run_name",
                               "constellation_config_sha256",
                               "ns3_version",
                               "state_semantics",
                               "coordinate_frame",
                               "coordinate_units",
                               "speed_of_light_m_per_s",
                               "simulation_duration_ns",
                               "trace_interval_ns",
                               "network_update_interval_ns",
                               "include_final_state",
                               "constellation",
                               "topology",
                               "randomness",
                               "slice_count",
                               "slices"},
                              "manifest root",
                              manifestPath);
    }
    else
    {
        FailManifest(manifestPath, "schema_version must equal 0.2 or 0.3");
    }
    RequireManifestString(root.at("ns3_version"), "ns3_version", "3.48", manifestPath);
    RequireManifestString(root.at("state_semantics"),
                          "state_semantics",
                          "orbit-policy-evaluation",
                          manifestPath);
    RequireManifestString(root.at("coordinate_frame"), "coordinate_frame", "ECEF", manifestPath);
    RequireManifestString(root.at("coordinate_units"), "coordinate_units", "m", manifestPath);
    if (ParseManifestUint(root.at("speed_of_light_m_per_s"),
                          "speed_of_light_m_per_s",
                          manifestPath) != 299792458)
    {
        FailManifest(manifestPath, "speed_of_light_m_per_s differs from 299792458");
    }
    ParseManifestTime(root.at("simulation_duration_ns"),
                      "simulation_duration_ns",
                      manifestPath,
                      true);
    ParseManifestTime(root.at("trace_interval_ns"), "trace_interval_ns", manifestPath, true);
    ParseManifestTime(root.at("network_update_interval_ns"),
                      "network_update_interval_ns",
                      manifestPath,
                      true);
    if (!root.at("include_final_state").is_boolean())
    {
        FailManifest(manifestPath, "include_final_state must be a boolean");
    }
    const std::string nameField = schemaVersion == "0.2" ? "scenario_name" : "run_name";
    const std::string runName =
        ParseManifestString(root.at(nameField), nameField, manifestPath);
    if (runName.empty())
    {
        FailManifest(manifestPath, nameField + " must not be empty");
    }
    const std::string hashField = schemaVersion == "0.2"
                                      ? "scenario_config_sha256"
                                      : "constellation_config_sha256";
    RequireSha256(ParseManifestString(root.at(hashField), hashField, manifestPath),
                  hashField,
                  manifestPath);

    RequireManifestFields(root.at("constellation"),
                          {"name",
                           "pattern",
                           "num_orbits",
                           "satellites_per_orbit",
                           "satellite_count",
                           "altitude_m",
                           "inclination_deg",
                           "phase_diff",
                           "orbit_epoch_offset_ns"},
                          "manifest constellation",
                          manifestPath);
    RequireManifestFields(root.at("topology"),
                          {"candidate_strategy",
                           "seam_enabled",
                           "max_isl_distance_m",
                           "delay_mode",
                           "fixed_delay_ns",
                           "link_bandwidth_bps"},
                          "manifest topology",
                          manifestPath);
    RequireManifestFields(root.at("randomness"),
                          {"seed", "run", "stream_start"},
                          "manifest randomness",
                          manifestPath);

    const Json& slices = root.at("slices");
    if (!slices.is_array() || slices.empty())
    {
        FailManifest(manifestPath, "slices must be a non-empty array");
    }
    const uint64_t sliceCount = ParseManifestUint(root.at("slice_count"),
                                                  "slice_count",
                                                  manifestPath);
    if (sliceCount != slices.size() || sliceCount > std::numeric_limits<uint32_t>::max())
    {
        FailManifest(manifestPath, "slice_count differs from slices array size");
    }

    std::map<int64_t, SnapshotFiles> filesByTime;
    std::set<std::string> listedFilenames;
    std::optional<int64_t> previousTimeNs;
    for (const Json& slice : slices)
    {
        RequireManifestFields(slice,
                              {"simulation_time_ns",
                               "nodes_file",
                               "nodes_sha256",
                               "topology_file",
                               "topology_sha256",
                               "active_link_count"},
                              "manifest slice",
                              manifestPath);
        const int64_t timeNs = ParseManifestTime(slice.at("simulation_time_ns"),
                                                 "slice.simulation_time_ns",
                                                 manifestPath);
        if (previousTimeNs && timeNs <= *previousTimeNs)
        {
            FailManifest(manifestPath, "manifest slice times must be strictly increasing");
        }
        previousTimeNs = timeNs;
        const std::string nodesFile =
            ParseManifestString(slice.at("nodes_file"), "nodes_file", manifestPath);
        const std::string topologyFile =
            ParseManifestString(slice.at("topology_file"), "topology_file", manifestPath);
        if (!listedFilenames.insert(nodesFile).second ||
            !listedFilenames.insert(topologyFile).second)
        {
            FailManifest(manifestPath, "manifest contains a duplicate slice filename");
        }
        SnapshotFiles files;
        files.nodesFilename = ResolveManifestFile(directory,
                                                  nodesFile,
                                                  "nodes_",
                                                  timeNs,
                                                  manifestPath);
        files.linksFilename = ResolveManifestFile(directory,
                                                  topologyFile,
                                                  "topology_",
                                                  timeNs,
                                                  manifestPath);
        const std::string nodesHash =
            ParseManifestString(slice.at("nodes_sha256"), "nodes_sha256", manifestPath);
        const std::string topologyHash = ParseManifestString(slice.at("topology_sha256"),
                                                             "topology_sha256",
                                                             manifestPath);
        VerifyManifestHash(files.nodesFilename, nodesHash, manifestPath);
        VerifyManifestHash(files.linksFilename, topologyHash, manifestPath);
        ParseManifestUint(slice.at("active_link_count"), "active_link_count", manifestPath);
        if (!filesByTime.emplace(timeNs, std::move(files)).second)
        {
            FailManifest(manifestPath, "manifest contains a duplicate slice time");
        }
    }
    return filesByTime;
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
    bool manifestAuthoritative = false;
    try
    {
        if (!std::filesystem::is_directory(directory))
        {
            throw TopologySnapshotError("cannot open satellite topology directory: " +
                                        directory.string());
        }
        const std::filesystem::path manifestPath = directory / "manifest.json";
        if (std::filesystem::is_regular_file(manifestPath))
        {
            filesByTime = ReadManifestFiles(directory, manifestPath);
            manifestAuthoritative = true;
        }
        else
        {
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
                    throw TopologySnapshotError("invalid snapshot timestamp filename: " +
                                                filename);
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
    schedule.manifestAuthoritative = manifestAuthoritative;
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
