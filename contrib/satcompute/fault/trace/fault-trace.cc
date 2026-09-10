/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Canonical observed events and an explicit validation-only frozen evidence reader.

#include "fault-trace.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace ns3
{

namespace
{

using OrderedJson = nlohmann::ordered_json;

[[noreturn]] void
Fail(const std::filesystem::path& filename,
     std::string_view field,
     std::string_view message)
{
    throw FaultTraceError(filename.string() + ": " + std::string(field) + " " +
                          std::string(message));
}

void
RejectOverlappingFaults(const std::filesystem::path& filename,
                        const std::vector<FaultDefinition>& faults)
{
    std::map<uint32_t, std::vector<const FaultDefinition*>> byNode;
    for (const FaultDefinition& fault : faults)
    {
        if (fault.faultOccurred)
        {
            byNode[fault.nodeId].push_back(&fault);
        }
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

void
ValidatePositiveDuration(const std::filesystem::path& filename,
                         std::string_view field,
                         const std::optional<int64_t>& duration)
{
    if (duration.has_value() && duration.value() <= 0)
    {
        Fail(filename, field, "must be null or a positive integer");
    }
}

void
ValidateNonNegativeTime(const std::filesystem::path& filename,
                        std::string_view field,
                        const std::optional<int64_t>& value)
{
    if (value.has_value() && value.value() < 0)
    {
        Fail(filename, field, "must be a non-negative integer or null");
    }
}

void
ValidateV2Fault(const std::filesystem::path& filename,
                const FaultDefinition& fault,
                std::optional<int64_t> simulationDurationNs)
{
    if (fault.faultId == 0)
    {
        Fail(filename, "fault_id", "must be positive");
    }
    if (!fault.faultOccurred || !fault.startTimeNs)
        Fail(filename, "fault_occurred/start_time_ns", "only occurred START records are supported");
    ValidateNonNegativeTime(filename, "start_time_ns", fault.startTimeNs);
    if (simulationDurationNs && *fault.startTimeNs >= *simulationDurationNs)
        Fail(filename, "start_time_ns", "must precede simulation stop");
    ValidatePositiveDuration(filename, "duration_ns", fault.durationNs);
    for (const auto& probability : {fault.failureProbability, fault.pF1, fault.pF2})
        if (probability && (!std::isfinite(*probability) || *probability < 0 || *probability > 1))
            Fail(filename, "failure_probability", "must be finite and in [0, 1]");
    if (fault.durationNs && *fault.startTimeNs > std::numeric_limits<int64_t>::max() - *fault.durationNs)
        Fail(filename, "duration_ns", "overflows recovery_time_ns");
    if (fault.faultType == FaultType::SATELLITE)
    {
        if (fault.failureProbability || fault.pF1 || fault.pF2 || fault.durationNs)
            Fail(filename, "satellite", "permanent F3 has no compute probability or duration");
    }
    else if (!fault.failureProbability || !fault.durationNs)
        Fail(filename, "compute", "requires START probability and positive duration");
}

template <typename T>
OrderedJson
OptionalJson(const std::optional<T>& value)
{
    return value.has_value() ? OrderedJson(value.value()) : OrderedJson(nullptr);
}

} // namespace

FaultTrace
ReadValidationFaultTrace(const std::filesystem::path& filename,
                         const std::vector<uint32_t>& satelliteIds,
                         int64_t durationNs)
{
    std::ifstream input(filename);
    if (!input || durationNs <= 0)
        Fail(filename, "file", "validation evidence unavailable or invalid horizon");
    const auto root = OrderedJson::parse(input);
    if (!root.is_object() || root.size() != 2 ||
        root.at("schema_version") != FAULT_TRACE_SCHEMA_VERSION || !root.at("faults").is_array())
        Fail(filename, "schema_version/faults", "expected canonical v2 evidence");
    const std::set<uint32_t> nodes(satelliteIds.begin(), satelliteIds.end());
    std::set<uint64_t> ids;
    FaultTrace trace;
    for (const auto& item : root.at("faults"))
    {
        if (!item.is_object() || item.size() != 13)
            Fail(filename, "fault", "expected all 13 canonical fields");
        const auto integer = [&](const char* key, uint64_t max) {
            const auto& value = item.at(key);
            if (!value.is_number_integer() || value < 0 || value > max)
                Fail(filename, key, "expected an in-range nonnegative integer");
            return value.get<uint64_t>();
        };
        const auto optionalNumber = [&](const char* key) -> std::optional<double> {
            const auto& value = item.at(key);
            if (value.is_null())
                return std::nullopt;
            if (!value.is_number() || !std::isfinite(value.get<double>()))
                Fail(filename, key, "expected finite number or null");
            return value.get<double>();
        };
        FaultDefinition f;
        f.faultId = integer("fault_id", std::numeric_limits<uint64_t>::max());
        f.nodeId = integer("node_id", std::numeric_limits<uint32_t>::max());
        const auto type = item.at("fault_type").get<std::string>();
        if (type != "compute" && type != "satellite")
            Fail(filename, "fault_type", "expected compute or satellite");
        f.faultType = type == "compute" ? FaultType::COMPUTE : FaultType::SATELLITE;
        f.faultOccurred = item.at("fault_occurred").get<bool>();
        f.startTimeNs = integer("start_time_ns", std::numeric_limits<int64_t>::max());
        if (!item.at("duration_ns").is_null())
            f.durationNs = integer("duration_ns", std::numeric_limits<int64_t>::max());
        f.failureProbability = optionalNumber("failure_probability");
        f.pF1 = optionalNumber("p_f1");
        f.pF2 = optionalNumber("p_f2");
        f.f1Occurred = item.at("f1_occurred").get<bool>();
        f.f2Occurred = item.at("f2_occurred").get<bool>();
        f.temperatureC = optionalNumber("temperature_c");
        f.continuousBusySeconds = optionalNumber("continuous_busy_s");
        ValidateV2Fault(filename, f, durationNs);
        if (!nodes.contains(f.nodeId) || !ids.insert(f.faultId).second)
            Fail(filename, "fault_id/node_id", "duplicate fault or unknown satellite");
        if (f.faultType == FaultType::COMPUTE &&
            (!f.pF1 || !f.pF2 || !(f.f1Occurred || f.f2Occurred)))
            Fail(filename, "compute", "requires source probabilities and occurred source");
        if (f.faultType == FaultType::SATELLITE && (f.f1Occurred || f.f2Occurred))
            Fail(filename, "satellite", "cannot carry compute source hits");
        trace.faults.push_back(f);
    }
    RejectOverlappingFaults(filename, trace.faults);
    return trace;
}

void
WriteFaultTraceV2(const std::filesystem::path& filename, const FaultTrace& trace)
{
    if (filename.empty())
    {
        throw FaultTraceError("fault trace output path must not be empty");
    }
    const std::filesystem::path outputPath =
        std::filesystem::absolute(filename).lexically_normal();
    if (trace.schemaVersion != FAULT_TRACE_SCHEMA_VERSION)
    {
        Fail(outputPath, "schema_version", "writer accepts only v2 traces");
    }

    std::set<uint64_t> faultIds;
    std::vector<const FaultDefinition*> canonical;
    canonical.reserve(trace.faults.size());
    for (const FaultDefinition& fault : trace.faults)
    {
        ValidateV2Fault(outputPath, fault, std::nullopt);
        if (!faultIds.insert(fault.faultId).second)
        {
            Fail(outputPath, "fault_id", "must be unique");
        }
        canonical.push_back(&fault);
    }
    RejectOverlappingFaults(outputPath, trace.faults);
    std::sort(canonical.begin(),
              canonical.end(),
              [](const FaultDefinition* left, const FaultDefinition* right) {
                  return std::make_tuple(left->GetAnchorTimeNs(),
                                         left->nodeId,
                                         left->faultId) <
                         std::make_tuple(right->GetAnchorTimeNs(),
                                         right->nodeId,
                                         right->faultId);
              });

    OrderedJson root;
    root["schema_version"] = FAULT_TRACE_SCHEMA_VERSION;
    root["faults"] = OrderedJson::array();
    for (const FaultDefinition* fault : canonical)
    {
        OrderedJson item;
        item["fault_id"] = fault->faultId;
        item["node_id"] = fault->nodeId;
        item["fault_type"] = FaultTypeToString(fault->faultType);
        item["fault_occurred"] = fault->faultOccurred;
        item["start_time_ns"] = OptionalJson(fault->startTimeNs);
        item["failure_probability"] = OptionalJson(fault->failureProbability);
        item["duration_ns"] = OptionalJson(fault->durationNs);
        item["p_f1"] = OptionalJson(fault->pF1);
        item["p_f2"] = OptionalJson(fault->pF2);
        item["f1_occurred"] = fault->f1Occurred;
        item["f2_occurred"] = fault->f2Occurred;
        item["temperature_c"] = OptionalJson(fault->temperatureC);
        item["continuous_busy_s"] = OptionalJson(fault->continuousBusySeconds);
        root["faults"].push_back(std::move(item));
    }

    std::error_code directoryError;
    std::filesystem::create_directories(outputPath.parent_path(), directoryError);
    if (directoryError)
    {
        Fail(outputPath, "file", "parent directory cannot be created");
    }
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        Fail(outputPath, "file", "cannot be opened for writing");
    }
    output << root.dump(2) << '\n';
    if (!output.good())
    {
        Fail(outputPath, "file", "could not be written completely");
    }
}

} // namespace ns3
