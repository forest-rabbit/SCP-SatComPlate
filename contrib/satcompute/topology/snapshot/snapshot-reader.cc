/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "snapshot-reader.h"

#include "../../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

struct ParsedNodes
{
    SatelliteSnapshotSchema schema{SatelliteSnapshotSchema::LEGACY};
    std::optional<int64_t> simulationTimeNs;
    std::vector<uint32_t> satelliteIds;
    std::vector<SatelliteSnapshotPosition> positions;
};

struct ParsedLinks
{
    SatelliteSnapshotSchema schema{SatelliteSnapshotSchema::LEGACY};
    std::optional<int64_t> simulationTimeNs;
    std::vector<SatelliteLink> links;
};

[[noreturn]] void
Fail(const std::filesystem::path& filename, const std::string& message)
{
    throw TopologySnapshotError(message + ": " + filename.string());
}

Json
ReadJsonFile(const std::filesystem::path& filename)
{
    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open())
    {
        Fail(filename, "cannot open topology snapshot");
    }

    try
    {
        Json root;
        input >> root;
        return root;
    }
    catch (const nlohmann::json::exception& error)
    {
        Fail(filename, "cannot parse topology JSON (" + std::string(error.what()) + ")");
    }
}

void
RequireExactFields(const Json& value,
                   std::initializer_list<std::string_view> expectedFields,
                   std::string_view context,
                   const std::filesystem::path& filename)
{
    if (!value.is_object())
    {
        Fail(filename, std::string(context) + " must be an object");
    }

    std::set<std::string> expected;
    for (const std::string_view field : expectedFields)
    {
        expected.emplace(field);
        if (!value.contains(std::string(field)))
        {
            Fail(filename,
                 std::string(context) + " is missing required field " + std::string(field));
        }
    }
    for (const auto& item : value.items())
    {
        if (!expected.contains(item.key()))
        {
            Fail(filename, std::string(context) + " contains unknown field " + item.key());
        }
    }
}

uint64_t
ParseUint64(const Json& value, std::string_view field, const std::filesystem::path& filename)
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
            if (parsed < 0)
            {
                Fail(filename, std::string(field) + " must be non-negative");
            }
            return static_cast<uint64_t>(parsed);
        }
    }
    catch (const nlohmann::json::exception& error)
    {
        Fail(filename, std::string(field) + " cannot be represented (" + error.what() + ")");
    }
    Fail(filename, std::string(field) + " must be an integer");
}

uint32_t
ParseUint32(const Json& value, std::string_view field, const std::filesystem::path& filename)
{
    const uint64_t parsed = ParseUint64(value, field, filename);
    if (parsed > std::numeric_limits<uint32_t>::max())
    {
        Fail(filename, std::string(field) + " exceeds uint32 range");
    }
    return static_cast<uint32_t>(parsed);
}

int64_t
ParseNonNegativeInt64(const Json& value,
                      std::string_view field,
                      const std::filesystem::path& filename)
{
    const uint64_t parsed = ParseUint64(value, field, filename);
    if (parsed > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        Fail(filename, std::string(field) + " exceeds int64 range");
    }
    return static_cast<int64_t>(parsed);
}

double
ParseFiniteNumber(const Json& value,
                  std::string_view field,
                  const std::filesystem::path& filename,
                  bool nonNegative = false)
{
    if (!value.is_number())
    {
        Fail(filename, std::string(field) + " must be a number");
    }
    double parsed;
    try
    {
        parsed = value.get<double>();
    }
    catch (const nlohmann::json::exception& error)
    {
        Fail(filename, std::string(field) + " cannot be represented (" + error.what() + ")");
    }
    if (!std::isfinite(parsed))
    {
        Fail(filename, std::string(field) + " must be finite");
    }
    if (nonNegative && parsed < 0.0)
    {
        Fail(filename, std::string(field) + " must be non-negative");
    }
    return parsed;
}

std::string
ParseString(const Json& value, std::string_view field, const std::filesystem::path& filename)
{
    if (!value.is_string())
    {
        Fail(filename, std::string(field) + " must be a string");
    }
    return value.get<std::string>();
}

void
RequireStringValue(const Json& value,
                   std::string_view field,
                   std::string_view expected,
                   const std::filesystem::path& filename)
{
    if (ParseString(value, field, filename) != expected)
    {
        Fail(filename,
             std::string(field) + " must equal " + std::string(expected));
    }
}

SatelliteSnapshotSchema
DetectSchema(const Json& root)
{
    return root.is_object() && root.contains("schema_version")
               ? SatelliteSnapshotSchema::VERSION_0_2
               : SatelliteSnapshotSchema::LEGACY;
}

ParsedNodes
ReadSatelliteNodes(const std::filesystem::path& filename)
{
    const Json root = ReadJsonFile(filename);
    ParsedNodes result;
    result.schema = DetectSchema(root);
    if (result.schema == SatelliteSnapshotSchema::LEGACY)
    {
        RequireExactFields(root, {"nodes"}, "nodes root", filename);
    }
    else
    {
        RequireExactFields(root,
                           {"schema_version",
                            "simulation_time_ns",
                            "state_semantics",
                            "coordinate_frame",
                            "coordinate_units",
                            "node_count",
                            "nodes"},
                           "nodes root",
                           filename);
        RequireStringValue(root.at("schema_version"), "schema_version", "0.2", filename);
        RequireStringValue(root.at("state_semantics"),
                           "state_semantics",
                           "orbit-policy-evaluation",
                           filename);
        RequireStringValue(root.at("coordinate_frame"), "coordinate_frame", "ECEF", filename);
        RequireStringValue(root.at("coordinate_units"), "coordinate_units", "m", filename);
        result.simulationTimeNs = ParseNonNegativeInt64(root.at("simulation_time_ns"),
                                                       "simulation_time_ns",
                                                       filename);
    }

    const Json& nodes = root.at("nodes");
    if (!nodes.is_array())
    {
        Fail(filename, "nodes must be an array");
    }
    if (result.schema == SatelliteSnapshotSchema::VERSION_0_2 &&
        ParseUint32(root.at("node_count"), "node_count", filename) != nodes.size())
    {
        Fail(filename, "node_count differs from nodes array size");
    }

    std::set<uint32_t> satelliteIds;
    std::optional<uint32_t> previousId;
    for (const Json& item : nodes)
    {
        if (result.schema == SatelliteSnapshotSchema::LEGACY)
        {
            RequireExactFields(item, {"node_id", "node_type"}, "nodes item", filename);
        }
        else
        {
            RequireExactFields(item,
                               {"node_id", "node_type", "x_m", "y_m", "z_m"},
                               "nodes item",
                               filename);
        }
        const uint32_t satelliteId = ParseUint32(item.at("node_id"), "node_id", filename);
        RequireStringValue(item.at("node_type"), "node_type", "sat", filename);
        if (!satelliteIds.insert(satelliteId).second)
        {
            Fail(filename, "nodes contains duplicate node_id " + std::to_string(satelliteId));
        }
        if (result.schema == SatelliteSnapshotSchema::VERSION_0_2)
        {
            if (previousId && satelliteId <= *previousId)
            {
                Fail(filename, "version 0.2 nodes must use ascending canonical node_id order");
            }
            result.positions.push_back({satelliteId,
                                        ParseFiniteNumber(item.at("x_m"), "x_m", filename),
                                        ParseFiniteNumber(item.at("y_m"), "y_m", filename),
                                        ParseFiniteNumber(item.at("z_m"), "z_m", filename)});
            previousId = satelliteId;
        }
    }
    if (satelliteIds.empty())
    {
        Fail(filename, "nodes contains no satellites");
    }
    result.satelliteIds.assign(satelliteIds.begin(), satelliteIds.end());
    return result;
}

ParsedLinks
ReadSatelliteLinks(const std::filesystem::path& filename)
{
    const Json root = ReadJsonFile(filename);
    ParsedLinks result;
    result.schema = DetectSchema(root);
    if (result.schema == SatelliteSnapshotSchema::LEGACY)
    {
        RequireExactFields(root, {"links"}, "topology root", filename);
    }
    else
    {
        RequireExactFields(root,
                           {"schema_version",
                            "simulation_time_ns",
                            "state_semantics",
                            "distance_units",
                            "delay_units",
                            "bandwidth_units",
                            "candidate_link_count",
                            "active_link_count",
                            "links"},
                           "topology root",
                           filename);
        RequireStringValue(root.at("schema_version"), "schema_version", "0.2", filename);
        RequireStringValue(root.at("state_semantics"),
                           "state_semantics",
                           "orbit-policy-evaluation",
                           filename);
        RequireStringValue(root.at("distance_units"), "distance_units", "m", filename);
        RequireStringValue(root.at("delay_units"), "delay_units", "ns", filename);
        RequireStringValue(root.at("bandwidth_units"), "bandwidth_units", "bps", filename);
        result.simulationTimeNs = ParseNonNegativeInt64(root.at("simulation_time_ns"),
                                                       "simulation_time_ns",
                                                       filename);
    }

    const Json& links = root.at("links");
    if (!links.is_array())
    {
        Fail(filename, "links must be an array");
    }
    if (result.schema == SatelliteSnapshotSchema::VERSION_0_2)
    {
        const uint32_t candidateCount = ParseUint32(root.at("candidate_link_count"),
                                                    "candidate_link_count",
                                                    filename);
        const uint32_t activeCount = ParseUint32(root.at("active_link_count"),
                                                 "active_link_count",
                                                 filename);
        if (activeCount != links.size())
        {
            Fail(filename, "active_link_count differs from links array size");
        }
        if (candidateCount < activeCount)
        {
            Fail(filename, "candidate_link_count is smaller than active_link_count");
        }
    }

    std::set<std::pair<uint32_t, uint32_t>> linkKeys;
    std::optional<std::pair<uint32_t, uint32_t>> previousKey;
    for (const Json& item : links)
    {
        uint32_t node1Id;
        uint32_t node2Id;
        int64_t delayNs;
        uint64_t bandwidthBps;
        if (result.schema == SatelliteSnapshotSchema::LEGACY)
        {
            RequireExactFields(item,
                               {"node1_id", "node2_id", "type", "delay", "link_bandwidth"},
                               "links item",
                               filename);
            node1Id = ParseUint32(item.at("node1_id"), "node1_id", filename);
            node2Id = ParseUint32(item.at("node2_id"), "node2_id", filename);
            const uint64_t delayUs = ParseUint64(item.at("delay"), "delay", filename);
            if (delayUs > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1000)
            {
                Fail(filename, "delay cannot be represented as integer nanoseconds");
            }
            delayNs = static_cast<int64_t>(delayUs * 1000);
            const uint64_t bandwidthKbps =
                ParseUint64(item.at("link_bandwidth"), "link_bandwidth", filename);
            if (bandwidthKbps == 0)
            {
                Fail(filename, "link_bandwidth must be positive");
            }
            if (bandwidthKbps > std::numeric_limits<uint64_t>::max() / 1000)
            {
                Fail(filename, "link_bandwidth exceeds bits-per-second range");
            }
            bandwidthBps = bandwidthKbps * 1000;
        }
        else
        {
            RequireExactFields(item,
                               {"node1_id",
                                "node2_id",
                                "type",
                                "candidate_kind",
                                "distance_m",
                                "delay_ns",
                                "link_bandwidth_bps"},
                               "links item",
                               filename);
            node1Id = ParseUint32(item.at("node1_id"), "node1_id", filename);
            node2Id = ParseUint32(item.at("node2_id"), "node2_id", filename);
            const std::string candidateKind =
                ParseString(item.at("candidate_kind"), "candidate_kind", filename);
            if (candidateKind != "intra-plane" && candidateKind != "inter-plane")
            {
                Fail(filename, "candidate_kind must be intra-plane or inter-plane");
            }
            ParseFiniteNumber(item.at("distance_m"), "distance_m", filename, true);
            delayNs = ParseNonNegativeInt64(item.at("delay_ns"), "delay_ns", filename);
            bandwidthBps =
                ParseUint64(item.at("link_bandwidth_bps"), "link_bandwidth_bps", filename);
            if (bandwidthBps == 0)
            {
                Fail(filename, "link_bandwidth_bps must be positive");
            }
            if (node1Id >= node2Id)
            {
                Fail(filename,
                     "version 0.2 links require node1_id smaller than node2_id");
            }
        }

        if (node1Id == node2Id)
        {
            Fail(filename, "satellite link cannot connect a node to itself");
        }
        RequireStringValue(item.at("type"), "type", "sat", filename);
        const auto endpoints = std::minmax(node1Id, node2Id);
        const std::pair<uint32_t, uint32_t> key{endpoints.first, endpoints.second};
        if (!linkKeys.insert(key).second)
        {
            Fail(filename,
                 "topology contains duplicate satellite link " + std::to_string(key.first) +
                     "<->" + std::to_string(key.second));
        }
        if (result.schema == SatelliteSnapshotSchema::VERSION_0_2 && previousKey &&
            key <= *previousKey)
        {
            Fail(filename, "version 0.2 links must use canonical endpoint order");
        }
        previousKey = key;
        result.links.push_back({key.first, key.second, delayNs, bandwidthBps});
    }

    if (result.schema == SatelliteSnapshotSchema::LEGACY)
    {
        std::sort(result.links.begin(),
                  result.links.end(),
                  [](const SatelliteLink& left, const SatelliteLink& right) {
                      return std::pair{left.sourceId, left.destinationId} <
                             std::pair{right.sourceId, right.destinationId};
                  });
    }
    return result;
}

} // namespace

SatelliteSnapshot
ReadSatelliteSnapshot(const std::filesystem::path& nodesFilename,
                      const std::filesystem::path& linksFilename,
                      std::optional<int64_t> expectedTimeNs)
{
    if (expectedTimeNs && *expectedTimeNs < 0)
    {
        throw TopologySnapshotError("expected snapshot time must be non-negative");
    }
    const ParsedNodes nodes = ReadSatelliteNodes(nodesFilename);
    const ParsedLinks links = ReadSatelliteLinks(linksFilename);
    if (nodes.schema != links.schema)
    {
        Fail(linksFilename, "paired nodes and topology snapshots use different schemas");
    }
    if (nodes.simulationTimeNs != links.simulationTimeNs)
    {
        Fail(linksFilename, "paired version 0.2 snapshots use different simulation times");
    }
    if (expectedTimeNs && nodes.simulationTimeNs &&
        *expectedTimeNs != *nodes.simulationTimeNs)
    {
        Fail(linksFilename,
             "embedded version 0.2 simulation time differs from its scheduled filename time");
    }

    SatelliteSnapshot snapshot;
    snapshot.schema = nodes.schema;
    snapshot.simulationTimeNs = nodes.simulationTimeNs;
    snapshot.satelliteIds = nodes.satelliteIds;
    snapshot.positions = nodes.positions;
    snapshot.links = links.links;

    const std::set<uint32_t> satelliteIds(snapshot.satelliteIds.begin(),
                                          snapshot.satelliteIds.end());
    for (const SatelliteLink& link : snapshot.links)
    {
        if (!satelliteIds.contains(link.sourceId) ||
            !satelliteIds.contains(link.destinationId))
        {
            std::ostringstream message;
            message << "ISL endpoint " << link.sourceId << "<->" << link.destinationId
                    << " is absent from paired nodes snapshot " << nodesFilename;
            Fail(linksFilename, message.str());
        }
    }
    return snapshot;
}

} // namespace ns3
