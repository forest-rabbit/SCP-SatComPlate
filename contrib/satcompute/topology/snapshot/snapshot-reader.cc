/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "snapshot-reader.h"

#include "../../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
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
            Fail(filename,
                 std::string(context) + " contains unknown field " + item.key());
        }
    }
}

uint64_t
ParseUint64(const Json& value,
            std::string_view field,
            const std::filesystem::path& filename)
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
        Fail(filename,
             std::string(field) + " cannot be represented (" + error.what() + ")");
    }
    Fail(filename, std::string(field) + " must be an integer");
}

uint32_t
ParseUint32(const Json& value,
            std::string_view field,
            const std::filesystem::path& filename)
{
    const uint64_t parsed = ParseUint64(value, field, filename);
    if (parsed > std::numeric_limits<uint32_t>::max())
    {
        Fail(filename, std::string(field) + " exceeds uint32 range");
    }
    return static_cast<uint32_t>(parsed);
}

std::string
ParseString(const Json& value,
            std::string_view field,
            const std::filesystem::path& filename)
{
    if (!value.is_string())
    {
        Fail(filename, std::string(field) + " must be a string");
    }
    return value.get<std::string>();
}

std::vector<uint32_t>
ReadSatelliteNodes(const std::filesystem::path& filename)
{
    const Json root = ReadJsonFile(filename);
    RequireExactFields(root, {"nodes"}, "nodes root", filename);
    const Json& nodes = root.at("nodes");
    if (!nodes.is_array())
    {
        Fail(filename, "nodes must be an array");
    }

    std::set<uint32_t> satelliteIds;
    for (const Json& item : nodes)
    {
        RequireExactFields(item, {"node_id", "node_type"}, "nodes item", filename);
        const uint32_t satelliteId = ParseUint32(item.at("node_id"), "node_id", filename);
        if (ParseString(item.at("node_type"), "node_type", filename) != "sat")
        {
            Fail(filename, "satellite-only replay requires node_type=sat");
        }
        if (!satelliteIds.insert(satelliteId).second)
        {
            Fail(filename, "nodes contains duplicate node_id " + std::to_string(satelliteId));
        }
    }
    if (satelliteIds.empty())
    {
        Fail(filename, "nodes contains no satellites");
    }
    return {satelliteIds.begin(), satelliteIds.end()};
}

std::vector<SatelliteLink>
ReadSatelliteLinks(const std::filesystem::path& filename)
{
    const Json root = ReadJsonFile(filename);
    RequireExactFields(root, {"links"}, "topology root", filename);
    const Json& links = root.at("links");
    if (!links.is_array())
    {
        Fail(filename, "links must be an array");
    }

    std::vector<SatelliteLink> parsedLinks;
    std::set<std::pair<uint32_t, uint32_t>> linkKeys;
    for (const Json& item : links)
    {
        RequireExactFields(item,
                           {"node1_id", "node2_id", "type", "delay", "link_bandwidth"},
                           "links item",
                           filename);
        const uint32_t node1Id = ParseUint32(item.at("node1_id"), "node1_id", filename);
        const uint32_t node2Id = ParseUint32(item.at("node2_id"), "node2_id", filename);
        if (node1Id == node2Id)
        {
            Fail(filename, "satellite link cannot connect a node to itself");
        }
        if (ParseString(item.at("type"), "type", filename) != "sat")
        {
            Fail(filename, "satellite-only replay requires type=sat");
        }

        const uint64_t delayUs = ParseUint64(item.at("delay"), "delay", filename);
        if (delayUs > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1000)
        {
            Fail(filename, "delay cannot be represented as integer nanoseconds");
        }
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

        const auto key = std::minmax(node1Id, node2Id);
        if (!linkKeys.emplace(key.first, key.second).second)
        {
            Fail(filename,
                 "topology contains duplicate satellite link " +
                     std::to_string(key.first) + "<->" + std::to_string(key.second));
        }
        parsedLinks.push_back(
            {key.first, key.second, delayUs, bandwidthKbps * 1000});
    }

    std::sort(parsedLinks.begin(),
              parsedLinks.end(),
              [](const SatelliteLink& left, const SatelliteLink& right) {
                  return std::pair{left.sourceId, left.destinationId} <
                         std::pair{right.sourceId, right.destinationId};
              });
    return parsedLinks;
}

} // namespace

SatelliteSnapshot
ReadSatelliteSnapshot(const std::filesystem::path& nodesFilename,
                      const std::filesystem::path& linksFilename)
{
    SatelliteSnapshot snapshot;
    snapshot.satelliteIds = ReadSatelliteNodes(nodesFilename);
    snapshot.links = ReadSatelliteLinks(linksFilename);

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
