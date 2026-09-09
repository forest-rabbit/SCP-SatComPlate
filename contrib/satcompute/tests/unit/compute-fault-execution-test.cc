/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/fault-controller.h"
#include "ns3/fault-para.h"
#include "ns3/fault-prediction-engine.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/satellite-id-map.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"

#include "../support/config-factory.h"
#include "../support/fault-injection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeOnlineTestConfig;
using satcompute::test::OnlineTestConfiguration;

constexpr int64_t MILLISECOND_NS = 1000000;
constexpr int64_t SIMULATION_DURATION_NS = 600 * MILLISECOND_NS;
constexpr uint32_t COMPUTE_NODE_ID = 3;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void
ResetSimulationGlobals()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

TaskDefinition
MakeTask(uint64_t taskId,
         int64_t arrivalTimeNs,
         uint64_t inputBytes,
         uint64_t computeWorkUnits,
         uint64_t outputBytes)
{
    return {taskId,
            0,
            COMPUTE_NODE_ID,
            0,
            inputBytes,
            outputBytes,
            computeWorkUnits,
            arrivalTimeNs,
            2 * taskId - 1,
            2 * taskId};
}

FaultDefinition
MakeComputeFault(uint64_t faultId,
                 int64_t startTimeNs,
                 std::optional<double> failureProbability,
                 int64_t durationNs)
{
    FaultDefinition fault;
    fault.faultId = faultId;
    fault.nodeId = COMPUTE_NODE_ID;
    fault.faultType = FaultType::COMPUTE;
    fault.startTimeNs = startTimeNs;
    fault.failureProbability = failureProbability;
    fault.durationNs = durationNs;
    fault.f1Occurred = true;
    return fault;
}

const TaskRuntime&
FindTask(const TaskCoordinator& coordinator, uint64_t taskId)
{
    const std::vector<TaskRuntime>& tasks = coordinator.GetTaskRuntimes();
    const auto task = std::find_if(tasks.begin(),
                                   tasks.end(),
                                   [taskId](const TaskRuntime& candidate) {
                                       return candidate.definition.taskId == taskId;
                                   });
    Check(task != tasks.end(), "missing task runtime");
    return *task;
}

const FaultRuntimeEventRecord&
FindFaultEvent(const std::vector<FaultRuntimeEventRecord>& events,
               uint64_t faultId,
               FaultEventType eventType)
{
    const auto event = std::find_if(events.begin(),
                                    events.end(),
                                    [faultId, eventType](
                                        const FaultRuntimeEventRecord& candidate) {
                                        return candidate.faultId == faultId &&
                                               candidate.eventType == eventType;
                                    });
    Check(event != events.end(), "missing fault runtime event");
    return *event;
}

struct ExecutionSignature
{
    std::vector<std::string> faultEvents;
    std::vector<std::string> predictions;
    std::vector<std::string> taskEvents;
    std::vector<std::string> transferTerminals;

    bool operator==(const ExecutionSignature&) const = default;
};

std::string
EncodePrediction(const ComputeFailureProbabilityRecord& prediction)
{
    std::ostringstream output;
    output << prediction.simulationTimeNs << ':'
           << prediction.nodeId << ':' << prediction.taskId << ':'
           << prediction.remainingComputeTimeNs << ':'
           << prediction.horizonStepCount << ':'
           << prediction.f1StepFailureProbability << ':'
           << prediction.f2StepFailureProbability << ':'
           << prediction.combinedStepFailureProbability << ':'
           << prediction.failureBeforeFinishProbability;
    return output.str();
}

std::string
EncodeFaultEvent(const FaultRuntimeEventRecord& event)
{
    std::ostringstream output;
    output << event.simulationTimeNs << ':' << event.faultId << ':'
           << FaultEventTypeToString(event.eventType) << ':'
           << event.satelliteAvailableAfter << ':'
           << event.communicationAvailableAfter << ':'
           << event.computeAvailableAfter << ':' << event.affectedTaskCount << ':'
           << event.affectedTransferCount << ':' << event.routeRecomputed;
    return output.str();
}

std::string
EncodeTaskEvent(const TaskEventRecord& event)
{
    std::ostringstream output;
    output << event.simulationTimeNs << ':' << event.taskId << ':'
           << TaskStateToString(event.fromState) << ':'
           << TaskStateToString(event.toState) << ':' << event.nodeId << ':'
           << event.cause;
    return output.str();
}

void
CheckFaultStateOverlay()
{
    FaultState state;
    state.Initialize({0, 1});
    FaultDefinition compute = MakeComputeFault(1, 10, std::nullopt, 5);
    compute.nodeId = 0;
    state.StartFault(compute);
    Check(state.IsSatelliteAvailable(0) && state.IsCommunicationAvailable(0) &&
              !state.IsComputeAvailable(0) && state.GetActiveFaultIds().contains(1),
          "compute fault availability overlay differs");
    state.RecoverFault(compute);
    Check(state.IsSatelliteAvailable(0) && state.IsCommunicationAvailable(0) &&
              state.IsComputeAvailable(0) && state.GetActiveFaultIds().empty(),
          "compute fault recovery overlay differs");

    FaultDefinition satellite;
    satellite.faultId = 2;
    satellite.nodeId = 1;
    satellite.faultType = FaultType::SATELLITE;
    satellite.startTimeNs = 20;
    satellite.durationNs = 5;
    state.StartFault(satellite);
    Check(!state.IsSatelliteAvailable(1) && !state.IsCommunicationAvailable(1) &&
              !state.IsComputeAvailable(1),
          "satellite fault availability overlay differs");
    state.RecoverFault(satellite);
    Check(state.IsSatelliteAvailable(1) && state.IsCommunicationAvailable(1) &&
              state.IsComputeAvailable(1),
          "satellite fault recovery overlay differs");
}

ExecutionSignature
RunComputeFaultScenario(bool generateOnline)
{
    OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                           8,
                                                           "fixed",
                                                           SIMULATION_DURATION_NS,
                                                           1000 * MILLISECOND_NS,
                                                           6171353.0L);
    config.parameters.fixedDelaySeconds = 0.00001;
    config.parameters.islBandwidthBps = 10000000;
    config.parameters.routingMode = "global-first";

    ExecutionSignature signature;
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const uint32_t routeComputationsBefore = topology.GetRouteComputationCount();
        const auto activeLinksBefore = topology.GetLinkState().GetActiveLinks();

        ComputeProfile profile;
        profile.nodes = {{COMPUTE_NODE_ID, 1000000000}};
        TaskTrace tasks;
        tasks.tasks = {
            MakeTask(1, 40 * MILLISECOND_NS, 100000, 1, 1),
            MakeTask(2, 30 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(3, 20 * MILLISECOND_NS, 1, 1000000000, 1),
            MakeTask(4, 10 * MILLISECOND_NS, 1, 1, 200000),
            MakeTask(5, 0, 1, 1, 1),
            MakeTask(6, 150 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(7, 300 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(8, 100 * MILLISECOND_NS, 1, 1, 1),
        };

        FaultTrace trace;
        trace.faults = {
            MakeComputeFault(1,
                             100 * MILLISECOND_NS,
                             0.8,
                             100 * MILLISECOND_NS),
            MakeComputeFault(2,
                             200 * MILLISECOND_NS,
                             0.5,
                             50 * MILLISECOND_NS),
        };
        Ptr<FaultController> controller = CreateObject<FaultController>();
        trace.faults[0].f2Occurred = true;
        trace.faults[1].f1Occurred = false;
        trace.faults[1].f2Occurred = true;
        Ptr<FaultPredictionEngine> predictionEngine =
            CreateObject<FaultPredictionEngine>();
        FaultParameters predictionParameters = GetDefaultFaultParameters();
        predictionParameters.checkIntervalSeconds = 0.01;
        predictionParameters.f1.temperature.riskC = 18.0;
        predictionParameters.f1.temperature.heatingToCriticalSeconds = 0.43;
        predictionParameters.f1.temperature.coolingFromCriticalToBaseSeconds = 0.4;
        predictionParameters.f1.enabled = true;
        predictionParameters.f2.enabled = false;
        predictionParameters.f3.enabled = false;
        predictionEngine->Configure(predictionParameters,
                                    {COMPUTE_NODE_ID},
                                    SIMULATION_DURATION_NS,
                                    controller);
        if (generateOnline)
        {
            controller->ConfigureGeneration(
                topology.GetIdMap().GetCanonicalSatelliteIds(),
                SIMULATION_DURATION_NS);
            const FaultDefinition firstFault = trace.faults[0];
            const FaultDefinition secondFault = trace.faults[1];
            Simulator::Schedule(
                NanoSeconds(firstFault.startTimeNs.value()),
                [controller, firstFault] {
                    controller->SubmitGeneratedBatch(
                        {{FaultEventType::START, firstFault}});
                });
            Simulator::Schedule(
                NanoSeconds(secondFault.startTimeNs.value()),
                [controller, secondFault] {
                    controller->SubmitGeneratedBatch(
                        {{FaultEventType::START, secondFault}});
                });
        }
        else
        {
            FaultControllerTestAccess::Schedule(controller,
                                                trace.faults,
                                                topology.GetIdMap().GetCanonicalSatelliteIds(),
                                                SIMULATION_DURATION_NS);
        }

        Ptr<TaskCoordinator> coordinator = CreateObject<TaskCoordinator>();
        coordinator->Initialize(profile,
                                tasks,
                                topology,
                                "fixed",
                                1024,
                                config.parameters.islMtuBytes,
                                config.parameters.receiverRcvBufBytes,
                                false,
                                SIMULATION_DURATION_NS);
        controller->BindTaskCoordinator(coordinator);
        predictionEngine->BindTaskCoordinator(coordinator);

        bool phasesChecked = false;
        Simulator::Schedule(NanoSeconds(99 * MILLISECOND_NS),
                            [coordinator, &phasesChecked] {
                                Check(FindTask(*coordinator, 1).state ==
                                          TASK_INPUT_TRANSFERRING,
                                      "input task did not reach fault boundary");
                                Check(FindTask(*coordinator, 2).state == TASK_QUEUED,
                                      "queued task did not reach fault boundary");
                                Check(FindTask(*coordinator, 3).state == TASK_RUNNING,
                                      "running task did not reach fault boundary");
                                Check(FindTask(*coordinator, 4).state ==
                                          TASK_RESULT_TRANSFERRING,
                                      "result task did not reach fault boundary");
                                Check(FindTask(*coordinator, 5).state == TASK_COMPLETED,
                                      "early task was not complete before fault");
                                phasesChecked = true;
                            });

        Simulator::Schedule(NanoSeconds(199 * MILLISECOND_NS), [coordinator] {
            for (const uint64_t id : std::vector<uint64_t>{1, 2, 6, 8})
            {
                const auto& task = FindTask(*coordinator, id);
                Check(task.state == TASK_QUEUED && task.computeStartTimeNs == -1 &&
                          task.computeDeadlineTimeNs == -1,
                      "outage did not retain unstarted queue without a deadline");
            }
            Check(!coordinator->GetComputeServices().front()->HasRunningTask(),
                  "outage dispatched work");
        });
        Simulator::Stop(NanoSeconds(SIMULATION_DURATION_NS));
        Simulator::Run();
        if (generateOnline)
        {
            controller->FinalizeGeneratedTrace(trace);
        }
        Check(phasesChecked, "pre-fault phase observer did not run");

        const std::vector<FaultRuntimeEventRecord>& faultEvents =
            controller->GetEvents();
        Check(faultEvents.size() == 4,
              "fault notice/start/recovery event count differs");
        const std::vector<std::string> expectedOrder = {
            "100000000:1:START",
            "200000000:1:RECOVERY",
            "200000000:2:START",
            "250000000:2:RECOVERY",
        };
        for (std::size_t index = 0; index < faultEvents.size(); ++index)
        {
            const FaultRuntimeEventRecord& event = faultEvents[index];
            const std::string prefix = std::to_string(event.simulationTimeNs) + ":" +
                                       std::to_string(event.faultId) + ":" +
                                       FaultEventTypeToString(event.eventType);
            Check(prefix == expectedOrder[index],
                  "same-timestamp fault event order differs");
            Check(event.satelliteAvailableAfter && event.communicationAvailableAfter &&
                      !event.routeRecomputed,
                  "compute fault changed communication or routing state");
            signature.faultEvents.push_back(EncodeFaultEvent(event));
        }
        const FaultRuntimeEventRecord& firstStart =
            FindFaultEvent(faultEvents, 1, FaultEventType::START);
        Check(firstStart.affectedTaskCount == 1 &&
                  firstStart.affectedTransferCount == 1 &&
                  !firstStart.computeAvailableAfter &&
                  firstStart.startTimeNs == 100 * MILLISECOND_NS,
              "first compute fault impact counts differ");
        const FaultRuntimeEventRecord& firstRecovery =
            FindFaultEvent(faultEvents, 1, FaultEventType::RECOVERY);
        const FaultRuntimeEventRecord& secondStart =
            FindFaultEvent(faultEvents, 2, FaultEventType::START);
        Check(firstRecovery.computeAvailableAfter &&
                  !secondStart.computeAvailableAfter &&
                  secondStart.affectedTaskCount == 0 &&
                  secondStart.affectedTransferCount == 0,
              "adjacent recovery/start final availability differs");
        Check(secondStart.failureProbability == 0.5,
              "failure probability metadata changed at runtime");
        Check(controller->GetState().GetActiveFaultIds().empty() &&
                  controller->GetState().IsComputeAvailable(COMPUTE_NODE_ID) &&
                  coordinator->IsComputeAvailable(COMPUTE_NODE_ID),
              "finite compute fault did not recover cleanly");

        for (const uint64_t taskId :
             std::vector<uint64_t>{3})
        {
            const TaskRuntime& task = FindTask(*coordinator, taskId);
            Check(task.state == TASK_FAILED &&
                      task.failureReason == TaskFailureReason::COMPUTE_NODE_FAILURE,
                  "compute fault did not leave task in permanent FAILED state");
        }
        for (const uint64_t taskId : std::vector<uint64_t>{1, 2, 4, 5, 6, 7, 8})
        {
            Check(FindTask(*coordinator, taskId).state == TASK_COMPLETED,
                  "unaffected or post-recovery task did not complete");
        }
        for (const uint64_t taskId : std::vector<uint64_t>{1, 2, 6, 8})
        {
            Check(FindTask(*coordinator, taskId).computeStartTimeNs >=
                      250 * MILLISECOND_NS,
                  "queued or in-outage arrival task started before recovery");
        }
        std::vector<const TaskRuntime*> resumed;
        for (const uint64_t id : std::vector<uint64_t>{1, 2, 6, 8})
        {
            resumed.push_back(&FindTask(*coordinator, id));
        }
        std::sort(resumed.begin(), resumed.end(), [](const auto* left, const auto* right) {
            return std::tie(left->queueEnterTimeNs, left->definition.taskId) <
                   std::tie(right->queueEnterTimeNs, right->definition.taskId);
        });
        for (std::size_t i = 1; i < resumed.size(); ++i)
        {
            Check(resumed[i - 1]->computeStartTimeNs < resumed[i]->computeStartTimeNs,
                  "recovery changed FCFS order");
        }

        Ptr<NetworkTransferEngine> transferEngine = coordinator->GetTransferEngine();
        for (const uint64_t transferId :
             std::vector<uint64_t>{6})
        {
            Check(transferEngine->GetTransferState(transferId) ==
                          TransferRuntimeState::CANCELLED &&
                      transferEngine->GetTerminalReason(transferId) ==
                          TransferTerminalReason::TASK_FAILED,
                  "failed task transfer was not cancelled exactly once");
        }
        Check(transferEngine->GetTransferState(3) == TransferRuntimeState::COMPLETED &&
                  transferEngine->GetTransferState(5) ==
                      TransferRuntimeState::COMPLETED &&
                  transferEngine->GetTransferState(7) ==
                      TransferRuntimeState::COMPLETED,
              "completed input transfer history was not preserved");
        for (const uint64_t transferId :
             std::vector<uint64_t>{1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16})
        {
            Check(transferEngine->GetTransferState(transferId) ==
                      TransferRuntimeState::COMPLETED,
                  "unaffected transfer did not finish normally");
        }

        const Ptr<ComputeService> service = coordinator->GetComputeServices().front();
        const auto& impacts = coordinator->GetFaultTaskImpacts();
        std::size_t interrupted = 0;
        bool sawLaterArrival = false;
        bool sawRepeatedQueue = false;
        for (const auto& record : impacts)
        {
            if (record.impactType == "RUNNING_INTERRUPTED")
            {
                ++interrupted;
                const auto& task = FindTask(*coordinator, record.taskId);
                Check(record.taskId == 3 && record.fault.f1Occurred && record.fault.f2Occurred &&
                          record.progressValid && record.impactTimeNs == 100 * MILLISECOND_NS &&
                          record.completedWorkUnits == static_cast<uint64_t>(
                              record.impactTimeNs - task.computeStartTimeNs) &&
                          record.completedWorkUnits + record.remainingWorkUnits ==
                              task.definition.computeWorkUnits &&
                          record.deadlineTimeNs == task.computeDeadlineTimeNs,
                      "direct impact source/progress/deadline snapshot differs");
            }
            else
            {
                Check(!record.progressValid && record.computeStartTimeNs == -1 &&
                          record.deadlineTimeNs == -1,
                      "indirect impact fabricated running progress or future deadline");
            }
            sawLaterArrival |= record.taskId == 6 &&
                record.impactType == "ARRIVAL_DURING_COMPUTE_OUTAGE" &&
                record.impactTimeNs == 150 * MILLISECOND_NS &&
                record.fault.startTimeNs == 100 * MILLISECOND_NS;
            sawRepeatedQueue |= record.taskId == 2 && record.fault.faultId == 2 &&
                record.impactType == "QUEUED_DELAYED";
        }
        Check(interrupted == 1 && sawLaterArrival && sawRepeatedQueue,
              "fault/task ledger lost source union, later admission, or a repeated impact");
        Check(service->IsComputeAvailable() && service->IsIdle() &&
                  service->GetEnqueuedTaskCount() == 8 && service->GetCompletedTaskCount() == 7 &&
                  service->GetCancelledRunningTaskCount() == 1 &&
                  service->GetRemovedQueuedTaskCount() == 0 &&
                  service->GetBusyTimeNs() ==
                      7 + static_cast<uint64_t>(FindTask(*coordinator, 3).failureTimeNs -
                                                FindTask(*coordinator, 3).computeStartTimeNs),
              "compute fault service accounting differs");
        Check(topology.GetRouteComputationCount() == routeComputationsBefore &&
                  topology.GetLinkState().GetActiveLinks() == activeLinksBefore,
              "compute fault modified ISLs or recomputed routes");

        const std::vector<ComputeFailureProbabilityRecord>& predictions =
            predictionEngine->GetPredictionRecords();
        Check(predictions.size() > 2 &&
                  predictions.front().simulationTimeNs < 90 * MILLISECOND_NS &&
                  predictions.back().simulationTimeNs == 100 * MILLISECOND_NS,
              "audit must cover pre-failure running checks without NOTICE");
        for (const ComputeFailureProbabilityRecord& prediction : predictions)
        {
            Check(prediction.nodeId == COMPUTE_NODE_ID &&
                      prediction.taskId == 3 &&
                      prediction.remainingComputeTimeNs > 0 &&
                      prediction.completionRatio > 0.0 &&
                      prediction.completionRatio < 1.0 &&
                      prediction.f1StepFailureProbability >= 0.0 &&
                      prediction.f2StepFailureProbability == 0.0 &&
                      std::abs(prediction.combinedStepFailureProbability -
                               prediction.f1StepFailureProbability) < 1e-15 &&
                      prediction.horizonStepCount > 0 &&
                      prediction.failureBeforeFinishProbability >=
                          prediction.combinedStepFailureProbability &&
                      prediction.failureBeforeFinishProbability <= 1.0 &&
                      prediction.expectedComputeCompletionTimeNs ==
                          prediction.simulationTimeNs +
                              prediction.remainingComputeTimeNs,
                  "model-driven prediction fields differ: " +
                      EncodePrediction(prediction));
            signature.predictions.push_back(EncodePrediction(prediction));
        }
        Check(predictions.back().f1StepFailureProbability >
                  predictions.front().f1StepFailureProbability &&
                  predictions[1].horizonStepCount + 1 ==
                      predictions[0].horizonStepCount,
              "rolling F1 forecast did not advance with task progress");

        for (const TaskEventRecord& event : coordinator->GetTaskEvents())
        {
            signature.taskEvents.push_back(EncodeTaskEvent(event));
        }
        for (const TransferSummaryRecord& transfer : transferEngine->CollectSummaries())
        {
            signature.transferTerminals.push_back(
                std::to_string(transfer.transferId) + ":" + transfer.transferState + ":" +
                transfer.terminalReason + ":" + std::to_string(transfer.terminalTimeNs));
        }
    }
    ResetSimulationGlobals();
    return signature;
}

} // namespace

int
main()
{
    try
    {
        CheckFaultStateOverlay();
        const ExecutionSignature first = RunComputeFaultScenario(false);
        const ExecutionSignature second = RunComputeFaultScenario(false);
        Check(first == second,
              "identical compute fault runs produced different lifecycle ordering");
        const ExecutionSignature generated = RunComputeFaultScenario(true);
        Check(first == generated,
              "online generated compute faults differ from test-only event injection");
        std::cout << "SatCompute compute fault execution tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
