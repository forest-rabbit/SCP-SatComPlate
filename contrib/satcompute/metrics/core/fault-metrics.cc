/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Persist deterministic fault events without feeding them back into execution.

#include "fault-metrics.h"

#include "ns3/fault-controller.h"
#include "ns3/fault-prediction-engine.h"
#include "../../task/task-coordinator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
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
WritePredictionMetrics(const FaultPredictionEngine& predictionEngine,
                       const std::vector<FaultRuntimeEventRecord>& events,
                       const TaskCoordinator& taskCoordinator,
                       int64_t simulationDurationNs,
                       const std::string& outputDirectory)
{
    if (simulationDurationNs <= 0)
    {
        throw std::runtime_error(
            "prediction metrics require a positive simulation duration");
    }
    const std::vector<ComputeFailurePredictionRecord>& predictions =
        predictionEngine.GetPredictionRecords();
    std::map<uint32_t, std::vector<int64_t>> computeStartTimesByNode;
    for (const FaultRuntimeEventRecord& event : events)
    {
        if (event.faultType == FaultType::COMPUTE &&
            event.eventType == FaultEventType::START)
        {
            computeStartTimesByNode[event.nodeId].push_back(event.simulationTimeNs);
        }
    }
    std::map<uint64_t, const TaskRuntime*> tasksById;
    for (const TaskRuntime& task : taskCoordinator.GetTaskRuntimes())
    {
        if (!tasksById.emplace(task.definition.taskId, &task).second)
        {
            throw std::runtime_error("prediction metrics found a duplicate task ID");
        }
    }

    std::ofstream output(OutputPath(outputDirectory, "fault-predictions.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write fault-predictions.csv");
    }
    output << "simulation_time_ns,fault_id,node_id,task_id,notice_time_ns,"
              "risk_elapsed_time_ns,task_compute_start_time_ns,task_service_time_ns,"
              "task_elapsed_time_ns,remaining_compute_time_ns,"
              "expected_compute_completion_time_ns,completion_ratio,"
              "f1_step_failure_probability,f2_step_failure_probability,"
              "combined_step_failure_probability,horizon_step_count,"
              "predicted_failure_probability,observed_compute_failure_before_finish\n";
    output << std::setprecision(17) << std::boolalpha;

    uint64_t observedFailureCount = 0;
    uint64_t evaluatedPredictionCount = 0;
    uint64_t censoredPredictionCount = 0;
    double predictedProbabilitySum = 0.0;
    double brierScoreSum = 0.0;
    double minimumProbability = 1.0;
    double maximumProbability = 0.0;
    std::set<uint64_t> episodeIds;
    std::set<uint64_t> taskIds;
    for (const ComputeFailurePredictionRecord& prediction : predictions)
    {
        const auto task = tasksById.find(prediction.taskId);
        if (task == tasksById.end() ||
            task->second->definition.computeNodeId != prediction.nodeId)
        {
            throw std::runtime_error(
                "prediction metrics cannot identify the running task");
        }
        const auto nodeStarts = computeStartTimesByNode.find(prediction.nodeId);
        std::optional<int64_t> targetFailureTimeNs;
        if (nodeStarts != computeStartTimesByNode.end())
        {
            const auto start = std::lower_bound(nodeStarts->second.begin(),
                                                nodeStarts->second.end(),
                                                prediction.simulationTimeNs);
            if (start != nodeStarts->second.end() &&
                *start <= prediction.expectedComputeCompletionTimeNs)
            {
                targetFailureTimeNs = *start;
            }
        }
        const TaskRuntime& runtime = *task->second;
        const std::optional<int64_t> competingFailureTimeNs =
            runtime.failureTimeNs >= prediction.simulationTimeNs &&
                    runtime.failureReason != TaskFailureReason::COMPUTE_NODE_FAILURE
                ? std::optional<int64_t>(runtime.failureTimeNs)
                : std::nullopt;
        const bool failureObserved =
            targetFailureTimeNs.has_value() &&
            (!competingFailureTimeNs.has_value() ||
             targetFailureTimeNs.value() <= competingFailureTimeNs.value());
        const bool fullyObservedWithoutFailure =
            !failureObserved && runtime.computeCompleteTimeNs >= prediction.simulationTimeNs &&
            runtime.computeCompleteTimeNs <=
                prediction.expectedComputeCompletionTimeNs;
        std::optional<bool> observedFailure;
        if (failureObserved)
        {
            observedFailure = true;
        }
        else if (fullyObservedWithoutFailure)
        {
            observedFailure = false;
        }
        if (observedFailure.has_value())
        {
            ++evaluatedPredictionCount;
            observedFailureCount += observedFailure.value() ? 1 : 0;
            predictedProbabilitySum += prediction.predictedFailureProbability;
            const double error = prediction.predictedFailureProbability -
                                 (observedFailure.value() ? 1.0 : 0.0);
            brierScoreSum += error * error;
            minimumProbability =
                std::min(minimumProbability,
                         prediction.predictedFailureProbability);
            maximumProbability =
                std::max(maximumProbability,
                         prediction.predictedFailureProbability);
        }
        else
        {
            ++censoredPredictionCount;
        }
        episodeIds.insert(prediction.faultId);
        taskIds.insert(prediction.taskId);

        output << prediction.simulationTimeNs << ',' << prediction.faultId << ','
               << prediction.nodeId << ',' << prediction.taskId << ','
               << prediction.noticeTimeNs << ',' << prediction.riskElapsedTimeNs << ','
               << prediction.taskComputeStartTimeNs << ','
               << prediction.taskServiceTimeNs << ','
               << prediction.taskElapsedTimeNs << ','
               << prediction.remainingComputeTimeNs << ','
               << prediction.expectedComputeCompletionTimeNs << ','
               << prediction.completionRatio << ','
               << prediction.f1StepFailureProbability << ','
               << prediction.f2StepFailureProbability << ','
               << prediction.combinedStepFailureProbability << ','
               << prediction.horizonStepCount << ','
               << prediction.predictedFailureProbability << ',';
        WriteOptionalCsv(output, observedFailure);
        output << '\n';
    }

    const bool hasEvaluatedPredictions = evaluatedPredictionCount > 0;
    const Json summary = {
        {"prediction_count", predictions.size()},
        {"evaluated_prediction_count", evaluatedPredictionCount},
        {"censored_prediction_count", censoredPredictionCount},
        {"risk_episode_count", episodeIds.size()},
        {"task_count", taskIds.size()},
        {"observed_failure_prediction_count", observedFailureCount},
        {"mean_predicted_failure_probability",
         hasEvaluatedPredictions
             ? Json(predictedProbabilitySum / evaluatedPredictionCount)
             : Json(nullptr)},
        {"observed_failure_rate",
         hasEvaluatedPredictions
             ? Json(static_cast<double>(observedFailureCount) /
                    evaluatedPredictionCount)
             : Json(nullptr)},
        {"brier_score",
         hasEvaluatedPredictions
             ? Json(brierScoreSum / evaluatedPredictionCount)
             : Json(nullptr)},
        {"minimum_predicted_failure_probability",
         hasEvaluatedPredictions ? Json(minimumProbability) : Json(nullptr)},
        {"maximum_predicted_failure_probability",
         hasEvaluatedPredictions ? Json(maximumProbability) : Json(nullptr)}};
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

} // namespace

void
WriteFaultMetrics(const FaultController& controller,
                  const FaultPredictionEngine* predictionEngine,
                  const TaskCoordinator* taskCoordinator,
                  const std::vector<TransferSummaryRecord>& transferSummaries,
                  int64_t simulationDurationNs,
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

    if (predictionEngine != nullptr)
    {
        if (taskCoordinator == nullptr)
        {
            throw std::runtime_error(
                "prediction metrics require a TaskCoordinator");
        }
        WritePredictionMetrics(*predictionEngine,
                               events,
                               *taskCoordinator,
                               simulationDurationNs,
                               outputDirectory);
    }
    else
    {
        const std::filesystem::path root =
            outputDirectory.empty() ? "." : outputDirectory;
        RemoveOwnedFile(root / "fault-predictions.csv");
        RemoveOwnedFile(root / "fault-prediction-summary.json");
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
}

} // namespace ns3
