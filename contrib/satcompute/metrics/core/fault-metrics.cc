/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Persist deterministic fault events without feeding them back into execution.

#include "fault-metrics.h"

#include "ns3/fault-controller.h"
#include "ns3/fault-model-engine.h"
#include "ns3/fault-prediction-engine.h"
#include "../../task/task-coordinator.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
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

void
WriteProbabilityRecords(const std::vector<ComputeFailureProbabilityRecord>& records,
                        const std::string& filename,
                        const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, filename),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write " + filename);
    }
    output << "simulation_time_ns,fault_id,node_id,task_id,notice_time_ns,"
              "risk_elapsed_time_ns,task_compute_start_time_ns,task_service_time_ns,"
              "task_elapsed_time_ns,remaining_compute_time_ns,"
              "expected_compute_completion_time_ns,completion_ratio,"
              "f1_step_failure_probability,f2_step_failure_probability,"
              "combined_step_failure_probability,horizon_step_count,"
              "failure_before_finish_probability\n";
    output << std::setprecision(17) << std::boolalpha;
    for (const ComputeFailureProbabilityRecord& record : records)
    {
        output << record.simulationTimeNs << ',' << record.faultId << ','
               << record.nodeId << ',' << record.taskId << ','
               << record.noticeTimeNs << ',' << record.riskElapsedTimeNs << ','
               << record.taskComputeStartTimeNs << ','
               << record.taskServiceTimeNs << ','
               << record.taskElapsedTimeNs << ','
               << record.remainingComputeTimeNs << ','
               << record.expectedComputeCompletionTimeNs << ','
               << record.completionRatio << ','
               << record.f1StepFailureProbability << ','
               << record.f2StepFailureProbability << ','
               << record.combinedStepFailureProbability << ','
               << record.horizonStepCount << ','
               << record.failureBeforeFinishProbability << '\n';
    }
}

void
WritePredictionMetrics(const FaultPredictionEngine& predictionEngine,
                       const std::string& outputDirectory)
{
    const std::vector<ComputeFailureProbabilityRecord>& predictions =
        predictionEngine.GetPredictionRecords();
    WriteProbabilityRecords(predictions,
                            "fault-predictions.csv",
                            outputDirectory);

    std::set<uint64_t> episodeIds;
    std::set<uint64_t> taskIds;
    for (const ComputeFailureProbabilityRecord& prediction : predictions)
    {
        episodeIds.insert(prediction.faultId);
        taskIds.insert(prediction.taskId);
    }
    const Json summary = {
        {"prediction_count", predictions.size()},
        {"risk_episode_count", episodeIds.size()},
        {"task_count", taskIds.size()}};
    std::ofstream summaryOutput(
        OutputPath(outputDirectory, "fault-prediction-summary.json"),
        std::ios::out | std::ios::trunc);
    if (!summaryOutput.is_open())
    {
        throw std::runtime_error(
            "cannot write fault-prediction-summary.json");
    }
    summaryOutput << summary.dump(2) << '\n';
}

const char*
FaultSource(const FaultDefinition& fault)
{
    if (fault.faultType == FaultType::SATELLITE)
    {
        return "F3";
    }
    if (fault.f1Occurred && fault.f2Occurred)
    {
        return "F1+F2";
    }
    return fault.f1Occurred ? "F1" : fault.f2Occurred ? "F2" : "UNSPECIFIED";
}

void
WriteTaskImpacts(const TaskCoordinator& coordinator, const std::string& outputDirectory)
{
    std::map<uint64_t, const TaskRuntime*> tasks;
    for (const auto& task : coordinator.GetTaskRuntimes())
    {
        tasks.emplace(task.definition.taskId, &task);
    }
    std::ofstream output(OutputPath(outputDirectory, "fault-task-impact.csv"));
    if (!output)
    {
        throw std::runtime_error("cannot write fault-task-impact.csv");
    }
    output << "fault_id,fault_type,fault_time_ns,impact_time_ns,fault_node_id,task_id,"
              "task_profile,input_bytes,output_bytes,compute_work_units,task_state_before_fault,"
              "task_state_before_impact,impact_type,compute_start_time_ns,"
              "completed_work_units_at_fault,remaining_work_units_at_fault,compute_progress_at_fault,"
              "progress_valid,baseline_compute_time_ns,compute_deadline_time_ns,"
              "deadline_slack_at_fault_ns,recoverable_outage_duration_ns,final_task_state,"
              "final_failure_reason\n";
    output << std::setprecision(17);
    for (const auto& record : coordinator.GetFaultTaskImpacts())
    {
        const auto& task = *tasks.at(record.taskId);
        const auto& def = task.definition;
        const auto& fault = record.fault;
        const auto faultTime = fault.startTimeNs.value();
        const char* atStart = record.impactTimeNs == faultTime
            ? TaskStateToString(record.stateBeforeImpact)
            : def.arrivalTimeNs > faultTime ? "NOT_ARRIVED" : "NOT_CAPTURED";
        output << fault.faultId << ',' << FaultSource(fault) << ',' << faultTime << ','
               << record.impactTimeNs << ',' << fault.nodeId << ',' << def.taskId << ','
               << TaskProfileToString(def.taskProfile) << ',' << def.inputBytes << ','
               << def.outputBytes << ',' << def.computeWorkUnits << ',' << atStart << ','
               << TaskStateToString(record.stateBeforeImpact) << ',' << record.impactType << ','
               << record.computeStartTimeNs << ',';
        if (record.progressValid)
        {
            output << record.completedWorkUnits << ',' << record.remainingWorkUnits << ','
                   << static_cast<double>(record.completedWorkUnits) / def.computeWorkUnits;
        }
        else
        {
            output << "-1,-1,-1";
        }
        output << ',' << static_cast<int>(record.progressValid) << ','
               << task.baselineComputeTimeNs << ',' << record.deadlineTimeNs << ','
               << (record.deadlineTimeNs < 0 ? -1 : record.deadlineTimeNs - faultTime) << ','
               << fault.durationNs.value_or(-1) << ','
               << (IsTerminalTaskState(task.state) ? TaskStateToString(task.state) : "TRUNCATED")
               << ',' << (task.state == TASK_FAILED ? TaskFailureReasonToString(task.failureReason) : "")
               << '\n';
    }
}

} // namespace

void
WriteFaultMetrics(const FaultController& controller,
                  const FaultModelEngine* modelEngine,
                  const FaultPredictionEngine* predictionEngine,
                  const TaskCoordinator* taskCoordinator,
                  const std::vector<TransferSummaryRecord>& transferSummaries,
                  const std::string& outputDirectory)
{
    const FaultTrace& trace = controller.GetTrace();
    const std::vector<FaultRuntimeEventRecord>& events = controller.GetEvents();
    std::map<uint64_t, const FaultDefinition*> faultById;
    for (const auto& fault : trace.faults)
    {
        faultById.emplace(fault.faultId, &fault);
    }

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
                   "route_recomputed,fault_source\n";
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
                    << event.affectedTransferCount << ',' << event.routeRecomputed << ','
                    << (event.eventType == FaultEventType::START
                            ? FaultSource(*faultById.at(event.faultId)) : "") << '\n';
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

    if (taskCoordinator != nullptr)
    {
        WriteTaskImpacts(*taskCoordinator, outputDirectory);
    }

    if (predictionEngine != nullptr)
    {
        if (taskCoordinator == nullptr)
        {
            throw std::runtime_error(
                "prediction metrics require a TaskCoordinator");
        }
        WritePredictionMetrics(*predictionEngine, outputDirectory);
    }
    else
    {
        const std::filesystem::path root =
            outputDirectory.empty() ? "." : outputDirectory;
        RemoveOwnedFile(root / "fault-predictions.csv");
        RemoveOwnedFile(root / "fault-prediction-summary.json");
    }
    const std::filesystem::path root =
        outputDirectory.empty() ? "." : outputDirectory;
    if (modelEngine != nullptr && predictionEngine != nullptr)
    {
        WriteProbabilityRecords(modelEngine->GetProbabilityRecords(),
                                "fault-model-probabilities.csv",
                                outputDirectory);
    }
    else
    {
        RemoveOwnedFile(root / "fault-model-probabilities.csv");
    }
}

void
RemoveFaultMetrics(const std::string& outputDirectory)
{
    const std::filesystem::path root = outputDirectory.empty() ? "." : outputDirectory;
    RemoveOwnedFile(root / "fault-events.csv");
    RemoveOwnedFile(root / "fault-summary.json");
    RemoveOwnedFile(root / "fault-predictions.csv");
    RemoveOwnedFile(root / "fault-prediction-summary.json");
    RemoveOwnedFile(root / "fault-model-probabilities.csv");
    RemoveOwnedFile(root / "fault-task-impact.csv");
}

} // namespace ns3
