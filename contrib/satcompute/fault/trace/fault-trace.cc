/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write canonical observed fault events; production has no trace reader.

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
