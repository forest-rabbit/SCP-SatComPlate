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
                 std::optional<int64_t> noticeTimeNs,
                 std::optional<double> failureProbability,
                 int64_t durationNs)
{
    FaultDefinition fault;
    fault.faultId = faultId;
    fault.nodeId = COMPUTE_NODE_ID;
    fault.faultType = FaultType::COMPUTE;
    fault.startTimeNs = startTimeNs;
    fault.noticeTimeNs = noticeTimeNs;
    fault.failureProbability = failureProbability;
    fault.warningLeadTimeNs = fault.GetWarningLeadTimeNs();
    fault.durationNs = durationNs;
    return fault;
}

GeneratedFaultEvent
MakeGeneratedNotice(const FaultDefinition& occurredFault)
{
    FaultDefinition notice = occurredFault;
    notice.faultOccurred = false;
    notice.startTimeNs = std::nullopt;
    notice.warningLeadTimeNs = std::nullopt;
    notice.durationNs = std::nullopt;
    return {FaultEventType::NOTICE, notice};
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
EncodePrediction(const ComputeFailurePredictionRecord& prediction)
{
    std::ostringstream output;
    output << prediction.simulationTimeNs << ':' << prediction.faultId << ':'
           << prediction.nodeId << ':' << prediction.taskId << ':'
           << prediction.noticeTimeNs << ':' << prediction.riskElapsedTimeNs << ':'
           << prediction.remainingComputeTimeNs << ':'
           << prediction.horizonStepCount << ':'
           << prediction.f1StepFailureProbability << ':'
           << prediction.f2StepFailureProbability << ':'
           << prediction.combinedStepFailureProbability << ':'
           << prediction.predictedFailureProbability;
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
    FaultDefinition compute = MakeComputeFault(1, 10, std::nullopt, std::nullopt, 5);
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

void
CheckRiskOnlyReplay()
{
    FaultDefinition riskOnly;
    riskOnly.faultId = 1;
    riskOnly.nodeId = COMPUTE_NODE_ID;
    riskOnly.faultType = FaultType::COMPUTE;
    riskOnly.faultOccurred = false;
    riskOnly.noticeTimeNs = 10 * MILLISECOND_NS;
    riskOnly.failureProbability = 0.25;
    riskOnly.riskDurationNs = 20 * MILLISECOND_NS;

    FaultTrace trace;
    trace.faults = {riskOnly};
    Ptr<FaultController> controller = CreateObject<FaultController>();
    controller->Configure(trace, {COMPUTE_NODE_ID}, SIMULATION_DURATION_NS);
    Simulator::Stop(NanoSeconds(50 * MILLISECOND_NS));
    Simulator::Run();

    const std::vector<FaultRuntimeEventRecord>& events = controller->GetEvents();
    Check(events.size() == 2 && events[0].eventType == FaultEventType::NOTICE &&
              events[1].eventType == FaultEventType::NOTICE_CLEAR &&
              events[0].simulationTimeNs == 10 * MILLISECOND_NS &&
              events[1].simulationTimeNs == 30 * MILLISECOND_NS,
          "risk-only NOTICE/NOTICE_CLEAR sequence differs");
    Check(!events[0].startTimeNs.has_value() && !events[0].durationNs.has_value() &&
              !events[0].warningLeadTimeNs.has_value() &&
              !events[0].riskDurationNs.has_value() &&
              events[0].failureProbability == 0.25,
          "NOTICE leaked future risk-only outcome fields");
    Check(!events[1].startTimeNs.has_value() &&
              events[1].riskDurationNs == 20 * MILLISECOND_NS &&
              controller->GetState().IsComputeAvailable(COMPUTE_NODE_ID),
          "NOTICE_CLEAR changed availability or lost observed duration");
    ResetSimulationGlobals();
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
                             90 * MILLISECOND_NS,
                             0.8,
                             100 * MILLISECOND_NS),
            MakeComputeFault(2,
                             200 * MILLISECOND_NS,
                             200 * MILLISECOND_NS,
                             0.5,
                             50 * MILLISECOND_NS),
        };
        Ptr<FaultController> controller = CreateObject<FaultController>();
        Ptr<FaultPredictionEngine> predictionEngine =
            CreateObject<FaultPredictionEngine>();
        FaultParameters predictionParameters = GetDefaultFaultParameters();
        predictionParameters.checkIntervalSeconds = 0.01;
        predictionParameters.f1.temperature.riskC = 18.0;
        predictionParameters.f1.temperature.heatingTauSeconds = 0.43;
        predictionParameters.f1.temperature.coolingTauSeconds = 0.4;
        predictionParameters.f1.maxFailureIntensityPerSecond = 0.5;
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
                NanoSeconds(firstFault.noticeTimeNs.value()),
                [controller, firstFault] {
                    controller->SubmitGeneratedBatch(
                        {MakeGeneratedNotice(firstFault)});
                });
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
                        {MakeGeneratedNotice(secondFault),
                         {FaultEventType::START, secondFault}});
                });
        }
        else
        {
            controller->Configure(trace,
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

        Simulator::Stop(NanoSeconds(SIMULATION_DURATION_NS));
        Simulator::Run();
        if (generateOnline)
        {
            controller->FinalizeGeneratedTrace(trace);
        }
        Check(phasesChecked, "pre-fault phase observer did not run");

        const std::vector<FaultRuntimeEventRecord>& faultEvents =
            controller->GetEvents();
        Check(faultEvents.size() == 6,
              "fault notice/start/recovery event count differs");
        const std::vector<std::string> expectedOrder = {
            "90000000:1:NOTICE",
            "100000000:1:START",
            "200000000:2:NOTICE",
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
        const FaultRuntimeEventRecord& firstNotice =
            FindFaultEvent(faultEvents, 1, FaultEventType::NOTICE);
        Check(!firstNotice.startTimeNs.has_value() &&
                  !firstNotice.durationNs.has_value() &&
                  !firstNotice.warningLeadTimeNs.has_value() &&
                  !firstNotice.riskDurationNs.has_value() &&
                  firstNotice.failureProbability == 0.8,
              "NOTICE leaked future occurred-fault fields");
        Check(firstStart.affectedTaskCount == 4 &&
                  firstStart.affectedTransferCount == 6 &&
                  !firstStart.computeAvailableAfter &&
                  firstStart.startTimeNs == 100 * MILLISECOND_NS,
              "first compute fault impact counts differ");
        const FaultRuntimeEventRecord& secondNotice =
            FindFaultEvent(faultEvents, 2, FaultEventType::NOTICE);
        const FaultRuntimeEventRecord& firstRecovery =
            FindFaultEvent(faultEvents, 1, FaultEventType::RECOVERY);
        const FaultRuntimeEventRecord& secondStart =
            FindFaultEvent(faultEvents, 2, FaultEventType::START);
        Check(!secondNotice.computeAvailableAfter &&
                  firstRecovery.computeAvailableAfter &&
                  !secondStart.computeAvailableAfter &&
                  secondStart.affectedTaskCount == 0 &&
                  secondStart.affectedTransferCount == 0,
              "adjacent recovery/start final availability differs");
        Check(secondNotice.failureProbability == 0.5,
              "failure probability metadata changed at runtime");
        Check(controller->GetState().GetActiveFaultIds().empty() &&
                  controller->GetState().IsComputeAvailable(COMPUTE_NODE_ID) &&
                  coordinator->IsComputeAvailable(COMPUTE_NODE_ID),
              "finite compute fault did not recover cleanly");

        for (const uint64_t taskId :
             std::vector<uint64_t>{1, 2, 3, 6, 8})
        {
            const TaskRuntime& task = FindTask(*coordinator, taskId);
            Check(task.state == TASK_FAILED &&
                      task.failureReason == TaskFailureReason::COMPUTE_NODE_FAILURE,
                  "compute fault did not leave task in permanent FAILED state");
        }
        for (const uint64_t taskId : std::vector<uint64_t>{4, 5, 7})
        {
            Check(FindTask(*coordinator, taskId).state == TASK_COMPLETED,
                  "unaffected or post-recovery task did not complete");
        }
        Check(FindTask(*coordinator, 8).failureTimeNs == 100 * MILLISECOND_NS &&
                  FindTask(*coordinator, 6).failureTimeNs == 150 * MILLISECOND_NS,
              "same-time or in-outage arrival failure time differs");

        Ptr<NetworkTransferEngine> transferEngine = coordinator->GetTransferEngine();
        for (const uint64_t transferId :
             std::vector<uint64_t>{1, 2, 4, 6, 11, 12, 15, 16})
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
             std::vector<uint64_t>{7, 8, 9, 10, 13, 14})
        {
            Check(transferEngine->GetTransferState(transferId) ==
                      TransferRuntimeState::COMPLETED,
                  "unaffected transfer did not finish normally");
        }

        const Ptr<ComputeService> service = coordinator->GetComputeServices().front();
        Check(service->IsComputeAvailable() && service->IsIdle() &&
                  service->GetEnqueuedTaskCount() == 5 &&
                  service->GetCompletedTaskCount() == 3 &&
                  service->GetCancelledRunningTaskCount() == 1 &&
                  service->GetRemovedQueuedTaskCount() == 1 &&
                  service->GetBusyTimeNs() == 3,
              "compute fault service accounting differs");
        Check(topology.GetRouteComputationCount() == routeComputationsBefore &&
                  topology.GetLinkState().GetActiveLinks() == activeLinksBefore,
              "compute fault modified ISLs or recomputed routes");

        const std::vector<ComputeFailurePredictionRecord>& predictions =
            predictionEngine->GetPredictionRecords();
        Check(predictions.size() == 2 &&
                  predictions[0].simulationTimeNs == 90 * MILLISECOND_NS &&
                  predictions[0].riskElapsedTimeNs == 0 &&
                  predictions[1].simulationTimeNs == 100 * MILLISECOND_NS &&
                  predictions[1].riskElapsedTimeNs == 10 * MILLISECOND_NS,
              "causal compute failure prediction record differs");
        for (const ComputeFailurePredictionRecord& prediction : predictions)
        {
            Check(prediction.faultId == 1 &&
                      prediction.nodeId == COMPUTE_NODE_ID &&
                      prediction.taskId == 3 &&
                      prediction.noticeTimeNs == 90 * MILLISECOND_NS &&
                      prediction.remainingComputeTimeNs > 0 &&
                      prediction.completionRatio > 0.0 &&
                      prediction.completionRatio < 1.0 &&
                      prediction.f1StepFailureProbability > 0.0 &&
                      prediction.f2StepFailureProbability == 0.0 &&
                      std::abs(prediction.combinedStepFailureProbability -
                               prediction.f1StepFailureProbability) < 1e-15 &&
                      prediction.horizonStepCount > 0 &&
                      prediction.predictedFailureProbability >=
                          prediction.combinedStepFailureProbability &&
                      prediction.predictedFailureProbability <= 1.0 &&
                      prediction.expectedComputeCompletionTimeNs ==
                          prediction.simulationTimeNs +
                              prediction.remainingComputeTimeNs,
                  "model-driven prediction fields differ: " +
                      EncodePrediction(prediction));
            signature.predictions.push_back(EncodePrediction(prediction));
        }
        Check(predictions[1].f1StepFailureProbability >
                  predictions[0].f1StepFailureProbability &&
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
        CheckRiskOnlyReplay();
        const ExecutionSignature first = RunComputeFaultScenario(false);
        const ExecutionSignature second = RunComputeFaultScenario(false);
        Check(first == second,
              "identical compute fault runs produced different lifecycle ordering");
        const ExecutionSignature generated = RunComputeFaultScenario(true);
        Check(first == generated,
              "online generated compute faults differ from deterministic replay");
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
