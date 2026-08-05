/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Parse task arrivals and derive their deterministic input/result transfer IDs.

#include "task-trace.h"

#include <nlohmann/json.hpp>

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
    throw TaskTraceError(filename.string() + ": " + std::string(field) + " " +
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

TaskTrace
ReadTaskTrace(const std::filesystem::path& filename,
              int64_t simulationDurationNs,
              const SatelliteEndpointView& endpoints,
              const ComputeProfile& computeProfile)
{
    if (filename.empty())
    {
        throw TaskTraceError("task trace path must not be empty");
    }
    if (simulationDurationNs <= 0)
    {
        throw TaskTraceError("simulation duration must be positive integer ns");
    }
    if (computeProfile.nodes.empty())
    {
        throw TaskTraceError("compute profile must not be empty");
    }

    const Json root = ReadJson(filename);
    RequireObjectFields(root, filename, "root", {"schema_version", "tasks"});
    const Json& schemaVersion = GetField(root, filename, "schema_version");
    if (!schemaVersion.is_string() || schemaVersion.get<std::string>() != "0.1")
    {
        Fail(filename, "schema_version", "must equal 0.1");
    }
    const Json& items = GetField(root, filename, "tasks");
    if (!items.is_array() || items.empty())
    {
        Fail(filename, "tasks", "must be a non-empty array");
    }

    TaskTrace trace;
    trace.tasks.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const Json& item = items[index];
        const std::string itemName = "tasks[" + std::to_string(index) + "]";
        RequireObjectFields(item,
                            filename,
                            itemName,
                            {"task_id",
                             "source_node_id",
                             "compute_node_id",
                             "result_node_id",
                             "input_bytes",
                             "output_bytes",
                             "compute_work_units",
                             "arrival_time_ns"});

        TaskDefinition task;
        task.taskId =
            RequireUint64(GetField(item, filename, "task_id"), filename, "task_id");
        if (task.taskId == 0 || task.taskId > std::numeric_limits<uint64_t>::max() / 2)
        {
            Fail(filename, "task_id", "must be in [1, UINT64_MAX/2]");
        }
        task.sourceNodeId = RequireUint32(GetField(item, filename, "source_node_id"),
                                          filename,
                                          "source_node_id");
        task.computeNodeId = RequireUint32(GetField(item, filename, "compute_node_id"),
                                           filename,
                                           "compute_node_id");
        task.resultNodeId = RequireUint32(GetField(item, filename, "result_node_id"),
                                          filename,
                                          "result_node_id");
        if (!endpoints.HasSatelliteId(task.sourceNodeId) ||
            !endpoints.HasSatelliteId(task.computeNodeId) ||
            !endpoints.HasSatelliteId(task.resultNodeId))
        {
            Fail(filename, itemName, "references an unknown satellite ID");
        }
        if (FindComputeNodeProfile(computeProfile, task.computeNodeId) == nullptr)
        {
            Fail(filename, "compute_node_id", "is absent from the compute profile");
        }
        if (task.sourceNodeId == task.computeNodeId)
        {
            Fail(filename, itemName, "must use different source and compute satellites");
        }
        if (task.computeNodeId == task.resultNodeId)
        {
            Fail(filename, itemName, "must use different compute and result satellites");
        }

        task.inputBytes =
            RequireUint64(GetField(item, filename, "input_bytes"), filename, "input_bytes");
        task.outputBytes = RequireUint64(GetField(item, filename, "output_bytes"),
                                         filename,
                                         "output_bytes");
        task.computeWorkUnits = RequireUint64(GetField(item, filename, "compute_work_units"),
                                              filename,
                                              "compute_work_units");
        if (task.inputBytes == 0 || task.outputBytes == 0 || task.computeWorkUnits == 0)
        {
            Fail(filename, itemName, "requires positive byte counts and compute work");
        }

        const uint64_t arrivalTimeNs = RequireUint64(GetField(item, filename, "arrival_time_ns"),
                                                     filename,
                                                     "arrival_time_ns");
        if (arrivalTimeNs >= static_cast<uint64_t>(simulationDurationNs))
        {
            Fail(filename, "arrival_time_ns", "must be earlier than simulation stop");
        }
        task.arrivalTimeNs = static_cast<int64_t>(arrivalTimeNs);
        task.inputTransferId = task.taskId * 2 - 1;
        task.resultTransferId = task.taskId * 2;
        trace.tasks.push_back(task);
    }

    std::sort(trace.tasks.begin(),
              trace.tasks.end(),
              [](const TaskDefinition& left, const TaskDefinition& right) {
                  return left.taskId < right.taskId;
              });
    for (std::size_t index = 1; index < trace.tasks.size(); ++index)
    {
        if (trace.tasks[index - 1].taskId == trace.tasks[index].taskId)
        {
            Fail(filename, "task_id", "must be unique");
        }
    }
    return trace;
}

} // namespace ns3
