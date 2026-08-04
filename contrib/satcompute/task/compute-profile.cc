/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Parse static per-satellite compute capacity without runtime side effects.

#include "compute-profile.h"

#include "../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

[[noreturn]] void
Fail(const std::filesystem::path& filename, std::string_view field, std::string_view message)
{
    throw ComputeProfileError(filename.string() + ": " + std::string(field) + " " +
                              std::string(message));
}

void
RequireObjectFields(const Json& value,
                    const std::filesystem::path& filename,
                    std::string_view name,
                    std::initializer_list<std::string_view> fields)
{
    if (!value.is_object())
    {
        Fail(filename, name, "must be an object");
    }
    std::set<std::string> expected;
    for (const std::string_view field : fields)
    {
        expected.emplace(field);
    }
    std::set<std::string> actual;
    for (const auto& item : value.items())
    {
        actual.insert(item.key());
    }
    if (actual != expected)
    {
        Fail(filename, name, "has missing or unknown fields");
    }
}

const Json&
GetField(const Json& object,
         const std::filesystem::path& filename,
         std::string_view field)
{
    const auto item = object.find(std::string(field));
    if (item == object.end())
    {
        Fail(filename, field, "is missing");
    }
    return *item;
}

uint64_t
RequireUint64(const Json& value,
              const std::filesystem::path& filename,
              std::string_view field)
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
    Fail(filename, field, "must be a non-negative integer");
}

uint32_t
RequireUint32(const Json& value,
              const std::filesystem::path& filename,
              std::string_view field)
{
    const uint64_t parsed = RequireUint64(value, filename, field);
    if (parsed > std::numeric_limits<uint32_t>::max())
    {
        Fail(filename, field, "exceeds uint32 range");
    }
    return static_cast<uint32_t>(parsed);
}

Json
ReadJson(const std::filesystem::path& filename)
{
    std::ifstream input(filename);
    if (!input.is_open())
    {
        Fail(filename, "file", "cannot be opened");
    }
    try
    {
        return Json::parse(input, nullptr, true, false);
    }
    catch (const std::exception& error)
    {
        Fail(filename, "JSON", error.what());
    }
}

} // namespace

ComputeProfile
ReadComputeProfile(const std::filesystem::path& filename,
                   const SatelliteEndpointView& endpoints)
{
    if (filename.empty())
    {
        throw ComputeProfileError("compute profile path must not be empty");
    }

    const Json root = ReadJson(filename);
    RequireObjectFields(root, filename, "root", {"schema_version", "compute_nodes"});
    const Json& schemaVersion = GetField(root, filename, "schema_version");
    if (!schemaVersion.is_string() || schemaVersion.get<std::string>() != "0.1")
    {
        Fail(filename, "schema_version", "must equal 0.1");
    }
    const Json& items = GetField(root, filename, "compute_nodes");
    if (!items.is_array() || items.empty())
    {
        Fail(filename, "compute_nodes", "must be a non-empty array");
    }

    ComputeProfile profile;
    profile.nodes.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const Json& item = items[index];
        const std::string itemName = "compute_nodes[" + std::to_string(index) + "]";
        RequireObjectFields(item,
                            filename,
                            itemName,
                            {"node_id", "compute_rate_work_units_per_second"});

        ComputeNodeProfile node;
        node.nodeId =
            RequireUint32(GetField(item, filename, "node_id"), filename, "node_id");
        node.computeRateWorkUnitsPerSecond =
            RequireUint64(GetField(item, filename, "compute_rate_work_units_per_second"),
                          filename,
                          "compute_rate_work_units_per_second");
        if (!endpoints.HasSatelliteId(node.nodeId))
        {
            Fail(filename, itemName, "references an unknown satellite ID");
        }
        if (node.computeRateWorkUnitsPerSecond == 0)
        {
            Fail(filename, "compute_rate_work_units_per_second", "must be positive");
        }
        profile.nodes.push_back(node);
    }

    std::sort(profile.nodes.begin(),
              profile.nodes.end(),
              [](const ComputeNodeProfile& left, const ComputeNodeProfile& right) {
                  return left.nodeId < right.nodeId;
              });
    for (std::size_t index = 1; index < profile.nodes.size(); ++index)
    {
        if (profile.nodes[index - 1].nodeId == profile.nodes[index].nodeId)
        {
            Fail(filename, "node_id", "must be unique");
        }
    }
    return profile;
}

const ComputeNodeProfile*
FindComputeNodeProfile(const ComputeProfile& profile, uint32_t nodeId)
{
    const auto node = std::lower_bound(profile.nodes.begin(),
                                       profile.nodes.end(),
                                       nodeId,
                                       [](const ComputeNodeProfile& candidate, uint32_t id) {
                                           return candidate.nodeId < id;
                                       });
    return node != profile.nodes.end() && node->nodeId == nodeId ? &(*node) : nullptr;
}

const ComputeNodeProfile&
GetComputeNodeProfile(const ComputeProfile& profile, uint32_t nodeId)
{
    const ComputeNodeProfile* node = FindComputeNodeProfile(profile, nodeId);
    if (node == nullptr)
    {
        throw ComputeProfileError("compute profile has no node_id=" + std::to_string(nodeId));
    }
    return *node;
}

} // namespace ns3
