/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Parse deterministic fault events without scheduling or changing runtime state.

#include "fault-trace.h"

#include "../task/compute-profile.h"
#include "../topology/satellite-endpoint-view.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

[[noreturn]] void
Fail(const std::filesystem::path& filename,
     std::string_view field,
     std::string_view message)
{
    throw FaultTraceError(filename.string() + ": " + std::string(field) + " " +
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

int64_t
RequireInt64Time(const Json& value,
                 const std::filesystem::path& filename,
                 std::string_view field)
{
    const uint64_t parsed = RequireUint64(value, filename, field);
    if (parsed > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        Fail(filename, field, "exceeds signed nanosecond range");
    }
    return static_cast<int64_t>(parsed);
}

std::optional<int64_t>
RequireOptionalTime(const Json& value,
                    const std::filesystem::path& filename,
                    std::string_view field)
{
    if (value.is_null())
    {
        return std::nullopt;
    }
    return RequireInt64Time(value, filename, field);
}

std::optional<double>
RequireOptionalProbability(const Json& value,
                           const std::filesystem::path& filename)
{
    if (value.is_null())
    {
        return std::nullopt;
    }
    if (!value.is_number())
    {
        Fail(filename, "failure_probability", "must be null or a number in [0, 1]");
    }
    double probability;
    try
    {
        probability = value.get<double>();
    }
    catch (const std::exception& error)
    {
        Fail(filename, "failure_probability", error.what());
    }
    if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0)
    {
        Fail(filename, "failure_probability", "must be finite and in [0, 1]");
    }
    return probability;
}

FaultType
RequireFaultType(const Json& value, const std::filesystem::path& filename)
{
    if (!value.is_string())
    {
        Fail(filename, "fault_type", "must be compute or satellite");
    }
    const std::string type = value.get<std::string>();
    if (type == "compute")
    {
        return FaultType::COMPUTE;
    }
    if (type == "satellite")
    {
        return FaultType::SATELLITE;
    }
    Fail(filename, "fault_type", "must be compute or satellite");
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

void
RejectOverlappingFaults(const std::filesystem::path& filename,
                        const std::vector<FaultDefinition>& faults)
{
    std::map<uint32_t, std::vector<const FaultDefinition*>> byNode;
    for (const FaultDefinition& fault : faults)
    {
        byNode[fault.nodeId].push_back(&fault);
    }
    for (auto& [nodeId, nodeFaults] : byNode)
    {
        std::sort(nodeFaults.begin(),
                  nodeFaults.end(),
                  [](const FaultDefinition* left, const FaultDefinition* right) {
                      return std::make_pair(left->startTimeNs.value(), left->faultId) <
                             std::make_pair(right->startTimeNs.value(), right->faultId);
                  });
        for (std::size_t index = 1; index < nodeFaults.size(); ++index)
        {
            const FaultDefinition& previous = *nodeFaults[index - 1];
            const FaultDefinition& current = *nodeFaults[index];
            const std::optional<int64_t> previousRecovery =
                previous.GetRecoveryTimeNs();
            if (!previousRecovery.has_value() ||
                current.startTimeNs.value() < previousRecovery.value())
            {
                Fail(filename,
                     "faults",
                     "contains overlapping intervals on node_id=" +
                         std::to_string(nodeId) + " for fault_id=" +
                         std::to_string(previous.faultId) + " and fault_id=" +
                         std::to_string(current.faultId));
            }
        }
    }
}

} // namespace

FaultTrace
ReadFaultTrace(const std::filesystem::path& filename,
               int64_t simulationDurationNs,
               const SatelliteEndpointView& endpoints,
               const ComputeProfile* computeProfile)
{
    if (filename.empty())
    {
        throw FaultTraceError("fault trace path must not be empty");
    }
    if (simulationDurationNs <= 0)
    {
        throw FaultTraceError("simulation duration must be positive integer ns");
    }

    const std::filesystem::path sourcePath =
        std::filesystem::absolute(filename).lexically_normal();
    const Json root = ReadJson(sourcePath);
    RequireObjectFields(root, sourcePath, "root", {"faults"});
    const Json& items = GetField(root, sourcePath, "faults");
    if (!items.is_array())
    {
        Fail(sourcePath, "faults", "must be an array");
    }

    FaultTrace trace;
    trace.sourcePath = sourcePath;
    trace.faults.reserve(items.size());
    std::set<uint64_t> faultIds;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const Json& item = items[index];
        const std::string itemName = "faults[" + std::to_string(index) + "]";
        RequireObjectFields(item,
                            sourcePath,
                            itemName,
                            {"fault_id",
                             "node_id",
                             "fault_type",
                             "start_time_ns",
                             "notice_time_ns",
                             "failure_probability",
                             "duration_ns"});

        FaultDefinition fault;
        fault.faultId =
            RequireUint64(GetField(item, sourcePath, "fault_id"),
                          sourcePath,
                          "fault_id");
        if (fault.faultId == 0)
        {
            Fail(sourcePath, "fault_id", "must be positive");
        }
        if (!faultIds.insert(fault.faultId).second)
        {
            Fail(sourcePath, "fault_id", "must be unique");
        }
        fault.nodeId = RequireUint32(GetField(item, sourcePath, "node_id"),
                                     sourcePath,
                                     "node_id");
        if (!endpoints.HasSatelliteId(fault.nodeId))
        {
            Fail(sourcePath, itemName, "references an unknown satellite ID");
        }
        fault.faultType = RequireFaultType(GetField(item, sourcePath, "fault_type"),
                                           sourcePath);
        if (fault.faultType == FaultType::COMPUTE &&
            (computeProfile == nullptr ||
             FindComputeNodeProfile(*computeProfile, fault.nodeId) == nullptr))
        {
            Fail(sourcePath,
                 itemName,
                 "compute fault node is absent from the ComputeProfile");
        }

        fault.startTimeNs = RequireInt64Time(
            GetField(item, sourcePath, "start_time_ns"),
            sourcePath,
            "start_time_ns");
        if (fault.startTimeNs.value() >= simulationDurationNs)
        {
            Fail(sourcePath, "start_time_ns", "must be earlier than simulation stop");
        }
        fault.noticeTimeNs = RequireOptionalTime(
            GetField(item, sourcePath, "notice_time_ns"),
            sourcePath,
            "notice_time_ns");
        fault.failureProbability = RequireOptionalProbability(
            GetField(item, sourcePath, "failure_probability"),
            sourcePath);
        if (fault.noticeTimeNs.has_value() != fault.failureProbability.has_value())
        {
            Fail(sourcePath,
                 itemName,
                 "notice_time_ns and failure_probability must both be null or non-null");
        }
        if (fault.noticeTimeNs.has_value() &&
            fault.noticeTimeNs.value() > fault.startTimeNs.value())
        {
            Fail(sourcePath, "notice_time_ns", "must not be later than start_time_ns");
        }

        fault.durationNs = RequireOptionalTime(
            GetField(item, sourcePath, "duration_ns"),
            sourcePath,
            "duration_ns");
        if (fault.durationNs.has_value())
        {
            if (fault.durationNs.value() <= 0)
            {
                Fail(sourcePath, "duration_ns", "must be null or a positive integer");
            }
            if (fault.startTimeNs.value() >
                std::numeric_limits<int64_t>::max() - fault.durationNs.value())
            {
                Fail(sourcePath, "duration_ns", "overflows recovery_time_ns");
            }
        }
        trace.faults.push_back(fault);
    }

    RejectOverlappingFaults(sourcePath, trace.faults);
    std::sort(trace.faults.begin(),
              trace.faults.end(),
              [](const FaultDefinition& left, const FaultDefinition& right) {
                  return left.faultId < right.faultId;
              });
    return trace;
}

} // namespace ns3
