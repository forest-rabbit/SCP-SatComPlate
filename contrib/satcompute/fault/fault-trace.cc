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
#include <tuple>
#include <utility>

namespace ns3
{

namespace
{

using Json = nlohmann::json;
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

bool
RequireBool(const Json& value,
            const std::filesystem::path& filename,
            std::string_view field)
{
    if (!value.is_boolean())
    {
        Fail(filename, field, "must be a boolean");
    }
    return value.get<bool>();
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
    if (fault.noticeTimeNs.has_value() && simulationDurationNs.has_value() &&
        fault.noticeTimeNs.value() >= simulationDurationNs.value())
    {
        Fail(filename, "notice_time_ns", "must be earlier than simulation stop");
    }
    if (fault.startTimeNs.has_value() && simulationDurationNs.has_value() &&
        fault.startTimeNs.value() >= simulationDurationNs.value())
    {
        Fail(filename, "start_time_ns", "must be earlier than simulation stop");
    }
    ValidatePositiveDuration(filename, "duration_ns", fault.durationNs);
    ValidatePositiveDuration(filename, "risk_duration_ns", fault.riskDurationNs);
    ValidateNonNegativeTime(filename, "notice_time_ns", fault.noticeTimeNs);
    ValidateNonNegativeTime(filename, "start_time_ns", fault.startTimeNs);
    ValidateNonNegativeTime(filename,
                            "warning_lead_time_ns",
                            fault.warningLeadTimeNs);
    if (fault.failureProbability.has_value() &&
        (!std::isfinite(fault.failureProbability.value()) ||
         fault.failureProbability.value() < 0.0 ||
         fault.failureProbability.value() > 1.0))
    {
        Fail(filename, "failure_probability", "must be finite and in [0, 1]");
    }
    if (fault.startTimeNs.has_value() && fault.durationNs.has_value() &&
        fault.startTimeNs.value() >
            std::numeric_limits<int64_t>::max() - fault.durationNs.value())
    {
        Fail(filename, "duration_ns", "overflows recovery_time_ns");
    }
    if (fault.noticeTimeNs.has_value() && fault.riskDurationNs.has_value() &&
        fault.noticeTimeNs.value() >
            std::numeric_limits<int64_t>::max() - fault.riskDurationNs.value())
    {
        Fail(filename, "risk_duration_ns", "overflows notice clear time");
    }
    if (fault.noticeTimeNs.has_value() && fault.riskDurationNs.has_value() &&
        simulationDurationNs.has_value() &&
        fault.noticeTimeNs.value() + fault.riskDurationNs.value() >
            simulationDurationNs.value())
    {
        Fail(filename, "risk_duration_ns", "extends beyond simulation stop");
    }

    if (fault.faultType == FaultType::SATELLITE)
    {
        if (!fault.faultOccurred)
        {
            Fail(filename, "fault_occurred", "must be true for satellite faults");
        }
        if (!fault.startTimeNs.has_value())
        {
            Fail(filename, "start_time_ns", "must be present for satellite faults");
        }
        if (fault.noticeTimeNs.has_value())
        {
            Fail(filename, "notice_time_ns", "must be null for satellite faults");
        }
        if (fault.failureProbability.has_value())
        {
            Fail(filename, "failure_probability", "must be null for satellite faults");
        }
        if (fault.warningLeadTimeNs.has_value())
        {
            Fail(filename,
                 "warning_lead_time_ns",
                 "must be null for satellite faults");
        }
        if (fault.riskDurationNs.has_value())
        {
            Fail(filename, "risk_duration_ns", "must be null for satellite faults");
        }
        if (fault.durationNs.has_value())
        {
            Fail(filename, "duration_ns", "must be null for satellite faults");
        }
        return;
    }

    if (!fault.failureProbability.has_value())
    {
        Fail(filename,
             "failure_probability",
             "must be present for compute records");
    }
    if (fault.faultOccurred)
    {
        if (!fault.startTimeNs.has_value())
        {
            Fail(filename, "start_time_ns", "must be present when fault_occurred is true");
        }
        if (!fault.durationNs.has_value())
        {
            Fail(filename, "duration_ns", "must be positive for an occurred compute fault");
        }
        if (fault.riskDurationNs.has_value())
        {
            Fail(filename,
                 "risk_duration_ns",
                 "must be null for an occurred compute fault");
        }
        if (fault.noticeTimeNs.has_value())
        {
            if (fault.noticeTimeNs.value() > fault.startTimeNs.value())
            {
                Fail(filename,
                     "notice_time_ns",
                     "must not be later than start_time_ns");
            }
            const int64_t expectedLead =
                fault.startTimeNs.value() - fault.noticeTimeNs.value();
            if (fault.warningLeadTimeNs != expectedLead)
            {
                Fail(filename,
                     "warning_lead_time_ns",
                     "must equal start_time_ns minus notice_time_ns");
            }
        }
        else if (fault.warningLeadTimeNs.has_value())
        {
            Fail(filename,
                 "warning_lead_time_ns",
                 "must be null without notice_time_ns");
        }
        return;
    }

    if (!fault.noticeTimeNs.has_value())
    {
        Fail(filename, "notice_time_ns", "must be present for a risk-only episode");
    }
    if (fault.startTimeNs.has_value())
    {
        Fail(filename, "start_time_ns", "must be null when fault_occurred is false");
    }
    if (fault.warningLeadTimeNs.has_value())
    {
        Fail(filename,
             "warning_lead_time_ns",
             "must be null for a risk-only episode");
    }
    if (!fault.riskDurationNs.has_value())
    {
        Fail(filename,
             "risk_duration_ns",
             "must be positive for a risk-only episode");
    }
    if (fault.durationNs.has_value())
    {
        Fail(filename, "duration_ns", "must be null for a risk-only episode");
    }
}

template <typename T>
OrderedJson
OptionalJson(const std::optional<T>& value)
{
    return value.has_value() ? OrderedJson(value.value()) : OrderedJson(nullptr);
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
    const bool isV2 = root.is_object() && root.contains("schema_version");
    if (isV2)
    {
        RequireObjectFields(root, sourcePath, "root", {"schema_version", "faults"});
        const uint32_t schemaVersion =
            RequireUint32(GetField(root, sourcePath, "schema_version"),
                          sourcePath,
                          "schema_version");
        if (schemaVersion != FAULT_TRACE_SCHEMA_VERSION)
        {
            Fail(sourcePath, "schema_version", "must equal 2");
        }
    }
    else
    {
        RequireObjectFields(root, sourcePath, "root", {"faults"});
    }
    const Json& items = GetField(root, sourcePath, "faults");
    if (!items.is_array())
    {
        Fail(sourcePath, "faults", "must be an array");
    }

    FaultTrace trace;
    trace.schemaVersion = isV2 ? FAULT_TRACE_SCHEMA_VERSION : 1;
    trace.sourcePath = sourcePath;
    trace.faults.reserve(items.size());
    std::set<uint64_t> faultIds;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const Json& item = items[index];
        const std::string itemName = "faults[" + std::to_string(index) + "]";
        if (isV2)
        {
            RequireObjectFields(item,
                                sourcePath,
                                itemName,
                                {"fault_id",
                                 "node_id",
                                 "fault_type",
                                 "fault_occurred",
                                 "notice_time_ns",
                                 "start_time_ns",
                                 "failure_probability",
                                 "warning_lead_time_ns",
                                 "risk_duration_ns",
                                 "duration_ns"});
        }
        else
        {
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
        }

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

        fault.faultOccurred =
            isV2 ? RequireBool(GetField(item, sourcePath, "fault_occurred"),
                               sourcePath,
                               "fault_occurred")
                 : true;
        fault.noticeTimeNs = RequireOptionalTime(
            GetField(item, sourcePath, "notice_time_ns"),
            sourcePath,
            "notice_time_ns");
        fault.startTimeNs =
            isV2 ? RequireOptionalTime(GetField(item, sourcePath, "start_time_ns"),
                                       sourcePath,
                                       "start_time_ns")
                 : std::optional<int64_t>(RequireInt64Time(
                       GetField(item, sourcePath, "start_time_ns"),
                       sourcePath,
                       "start_time_ns"));
        fault.failureProbability = RequireOptionalProbability(
            GetField(item, sourcePath, "failure_probability"),
            sourcePath);
        fault.warningLeadTimeNs =
            isV2 ? RequireOptionalTime(
                       GetField(item, sourcePath, "warning_lead_time_ns"),
                       sourcePath,
                       "warning_lead_time_ns")
                 : std::nullopt;
        fault.riskDurationNs =
            isV2 ? RequireOptionalTime(GetField(item, sourcePath, "risk_duration_ns"),
                                       sourcePath,
                                       "risk_duration_ns")
                 : std::nullopt;
        fault.durationNs = RequireOptionalTime(
            GetField(item, sourcePath, "duration_ns"),
            sourcePath,
            "duration_ns");

        if (isV2)
        {
            ValidateV2Fault(sourcePath, fault, simulationDurationNs);
        }
        else
        {
            if (fault.startTimeNs.value() >= simulationDurationNs)
            {
                Fail(sourcePath,
                     "start_time_ns",
                     "must be earlier than simulation stop");
            }
            if (fault.noticeTimeNs.has_value() !=
                fault.failureProbability.has_value())
            {
                Fail(sourcePath,
                     itemName,
                     "notice_time_ns and failure_probability must both be null or non-null");
            }
            if (fault.noticeTimeNs.has_value() &&
                fault.noticeTimeNs.value() > fault.startTimeNs.value())
            {
                Fail(sourcePath,
                     "notice_time_ns",
                     "must not be later than start_time_ns");
            }
            fault.warningLeadTimeNs = fault.GetWarningLeadTimeNs();
            ValidatePositiveDuration(sourcePath, "duration_ns", fault.durationNs);
            if (fault.startTimeNs.value() >
                std::numeric_limits<int64_t>::max() -
                    fault.durationNs.value_or(0))
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
        item["notice_time_ns"] = OptionalJson(fault->noticeTimeNs);
        item["start_time_ns"] = OptionalJson(fault->startTimeNs);
        item["failure_probability"] = OptionalJson(fault->failureProbability);
        item["warning_lead_time_ns"] = OptionalJson(fault->warningLeadTimeNs);
        item["risk_duration_ns"] = OptionalJson(fault->riskDurationNs);
        item["duration_ns"] = OptionalJson(fault->durationNs);
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
