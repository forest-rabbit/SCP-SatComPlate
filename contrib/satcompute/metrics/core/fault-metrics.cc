/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Persist deterministic fault events without feeding them back into execution.

#include "fault-metrics.h"

#include "ns3/fault-controller.h"
#include "../../task/task-coordinator.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

std::filesystem::path
OutputPath(const std::string& directory, const std::string& filename)
{
    const std::filesystem::path root = directory.empty() ? "." : directory;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        throw std::runtime_error("cannot create metrics directory " + root.string() + ": " +
                                 error.message());
    }
    return root / filename;
}

template <typename T>
void
WriteOptionalCsv(std::ostream& output, const std::optional<T>& value)
{
    if (value.has_value())
    {
        output << value.value();
    }
}

void
RemoveOwnedFile(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        return;
    }
    if (error)
    {
        throw std::runtime_error("cannot inspect fault metric " + path.string() + ": " +
                                 error.message());
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return;
    }
    std::filesystem::remove(path, error);
    if (error)
    {
        throw std::runtime_error("cannot remove fault metric " + path.string() + ": " +
                                 error.message());
    }
}

} // namespace

void
WriteFaultMetrics(const FaultController& controller,
                  const TaskCoordinator* taskCoordinator,
                  const std::vector<TransferSummaryRecord>& transferSummaries,
                  const std::string& outputDirectory)
{
    const FaultTrace& trace = controller.GetTrace();
    const std::vector<FaultRuntimeEventRecord>& events = controller.GetEvents();

    std::ofstream eventOutput(OutputPath(outputDirectory, "fault-events.csv"),
                              std::ios::out | std::ios::trunc);
    if (!eventOutput.is_open())
    {
        throw std::runtime_error("cannot write fault-events.csv");
    }
    eventOutput << "simulation_time_ns,fault_id,node_id,fault_type,event_type,"
                   "notice_time_ns,start_time_ns,duration_ns,failure_probability,"
                   "satellite_available_after,communication_available_after,"
                   "compute_available_after,affected_task_count,affected_transfer_count,"
                   "route_recomputed\n";
    eventOutput << std::setprecision(17) << std::boolalpha;
    for (const FaultRuntimeEventRecord& event : events)
    {
        eventOutput << event.simulationTimeNs << ',' << event.faultId << ',' << event.nodeId
                    << ',' << FaultTypeToString(event.faultType) << ','
                    << FaultEventTypeToString(event.eventType) << ',';
        WriteOptionalCsv(eventOutput, event.noticeTimeNs);
        eventOutput << ',';
        WriteOptionalCsv(eventOutput, event.startTimeNs);
        eventOutput << ',';
        WriteOptionalCsv(eventOutput, event.durationNs);
        eventOutput << ',';
        WriteOptionalCsv(eventOutput, event.failureProbability);
        eventOutput << ',' << event.satelliteAvailableAfter << ','
                    << event.communicationAvailableAfter << ','
                    << event.computeAvailableAfter << ',' << event.affectedTaskCount << ','
                    << event.affectedTransferCount << ',' << event.routeRecomputed << '\n';
    }

    uint64_t computeFaultCount = 0;
    uint64_t satelliteFaultCount = 0;
    for (const FaultDefinition& fault : trace.faults)
    {
        if (fault.faultType == FaultType::COMPUTE)
        {
            ++computeFaultCount;
        }
        else
        {
            ++satelliteFaultCount;
        }
    }
    uint64_t noticeEventCount = 0;
    uint64_t startEventCount = 0;
    uint64_t recoveryEventCount = 0;
    uint64_t routeRecomputationCount = 0;
    for (const FaultRuntimeEventRecord& event : events)
    {
        switch (event.eventType)
        {
        case FaultEventType::NOTICE:
            ++noticeEventCount;
            break;
        case FaultEventType::NOTICE_CLEAR:
            break;
        case FaultEventType::START:
            ++startEventCount;
            break;
        case FaultEventType::RECOVERY:
            ++recoveryEventCount;
            break;
        }
        if (event.routeRecomputed)
        {
            ++routeRecomputationCount;
        }
    }

    uint64_t failedTaskCount = 0;
    if (taskCoordinator != nullptr)
    {
        for (const TaskRuntime& task : taskCoordinator->GetTaskRuntimes())
        {
            if (task.state == TASK_FAILED)
            {
                ++failedTaskCount;
            }
        }
    }
    uint64_t failedTransferCount = 0;
    uint64_t cancelledTransferCount = 0;
    for (const TransferSummaryRecord& transfer : transferSummaries)
    {
        if (transfer.transferState == "FAILED")
        {
            ++failedTransferCount;
        }
        else if (transfer.transferState == "CANCELLED")
        {
            ++cancelledTransferCount;
        }
    }

    const Json summary = {
        {"fault_count", trace.faults.size()},
        {"compute_fault_count", computeFaultCount},
        {"satellite_fault_count", satelliteFaultCount},
        {"notice_event_count", noticeEventCount},
        {"start_event_count", startEventCount},
        {"recovery_event_count", recoveryEventCount},
        {"active_fault_count_at_end", controller.GetState().GetActiveFaultIds().size()},
        {"failed_task_count", failedTaskCount},
        {"failed_transfer_count", failedTransferCount},
        {"cancelled_transfer_count", cancelledTransferCount},
        {"route_recomputation_count_due_to_fault", routeRecomputationCount}};
    std::ofstream summaryOutput(OutputPath(outputDirectory, "fault-summary.json"),
                                std::ios::out | std::ios::trunc);
    if (!summaryOutput.is_open())
    {
        throw std::runtime_error("cannot write fault-summary.json");
    }
    summaryOutput << summary.dump(2) << '\n';
}

void
RemoveFaultMetrics(const std::string& outputDirectory)
{
    const std::filesystem::path root = outputDirectory.empty() ? "." : outputDirectory;
    RemoveOwnedFile(root / "fault-events.csv");
    RemoveOwnedFile(root / "fault-summary.json");
}

} // namespace ns3
