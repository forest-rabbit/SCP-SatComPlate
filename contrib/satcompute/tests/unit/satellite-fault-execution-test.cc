/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/capacity-reservation-state.h"
#include "ns3/fault-controller.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer-config.h"
#include "ns3/network-transfer-engine.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/online-topology-controller.h"
#include "ns3/plus-grid-candidate.h"
#include "ns3/routing-policy-factory.h"
#include "ns3/satellite-id-map.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"

#include "../support/config-factory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeOnlineTestConfig;
using satcompute::test::MakeTestConstellation;
using satcompute::test::OnlineTestConfiguration;

constexpr int64_t MILLISECOND_NS = 1000000;
constexpr int64_t TASK_SIMULATION_DURATION_NS = 900 * MILLISECOND_NS;

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

double
Distance(const Vector& first, const Vector& second)
{
    const double x = first.x - second.x;
    const double y = first.y - second.y;
    const double z = first.z - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

TaskDefinition
MakeTask(uint64_t taskId,
         uint32_t sourceNodeId,
         uint32_t computeNodeId,
         uint32_t resultNodeId,
         int64_t arrivalTimeNs,
         uint64_t inputBytes,
         uint64_t computeWorkUnits,
         uint64_t outputBytes)
{
    return {taskId,
            sourceNodeId,
            computeNodeId,
            resultNodeId,
            inputBytes,
            outputBytes,
            computeWorkUnits,
            arrivalTimeNs,
            2 * taskId - 1,
            2 * taskId};
}

FaultDefinition
MakeSatelliteFault(uint64_t faultId,
                   uint32_t nodeId,
                   int64_t startTimeNs,
                   int64_t durationNs,
                   std::optional<int64_t> noticeTimeNs = std::nullopt,
                   std::optional<double> probability = std::nullopt)
{
    FaultDefinition fault;
    fault.faultId = faultId;
    fault.nodeId = nodeId;
    fault.faultType = FaultType::SATELLITE;
    fault.startTimeNs = startTimeNs;
    fault.noticeTimeNs = noticeTimeNs;
    fault.failureProbability = probability;
    fault.durationNs = durationNs;
    return fault;
}

const TaskRuntime&
FindTask(const TaskCoordinator& coordinator, uint64_t taskId)
{
    const auto& tasks = coordinator.GetTaskRuntimes();
    const auto task = std::find_if(tasks.begin(),
                                   tasks.end(),
                                   [taskId](const TaskRuntime& candidate) {
                                       return candidate.definition.taskId == taskId;
                                   });
    Check(task != tasks.end(), "missing satellite-fault task runtime");
    return *task;
}

const FaultRuntimeEventRecord&
FindFaultEvent(const FaultController& controller,
               uint64_t faultId,
               FaultEventType eventType)
{
    const auto& events = controller.GetEvents();
    const auto event = std::find_if(events.begin(),
                                    events.end(),
                                    [faultId, eventType](const auto& candidate) {
                                        return candidate.faultId == faultId &&
                                               candidate.eventType == eventType;
                                    });
    Check(event != events.end(), "missing satellite fault event");
    return *event;
}

bool
HasIncidentActiveLink(const OnlineTopologyController& topology, uint32_t nodeId)
{
    const auto& links = topology.GetLinkState().GetActiveLinks();
    return std::any_of(links.begin(),
                       links.end(),
                       [nodeId](const auto& link) {
                           return link.first == nodeId || link.second == nodeId;
                       });
}

void
CheckTransferTerminal(Ptr<NetworkTransferEngine> engine,
                      uint64_t transferId,
                      TransferRuntimeState state,
                      TransferTerminalReason reason)
{
    Check(engine->GetTransferState(transferId) == state &&
              engine->GetTerminalReason(transferId) == reason,
          "satellite-fault transfer terminal state or reason differs");
}

void
RunTaskAndTopologyCase()
{
    OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                           8,
                                                           "fixed",
                                                           TASK_SIMULATION_DURATION_NS,
                                                           2000 * MILLISECOND_NS,
                                                           6171353.0L);
    config.parameters.fixedDelaySeconds = 0.00001;
    config.parameters.islBandwidthBps = 10000000;
    config.parameters.routingMode = "global-first";

    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const auto initialLinks = topology.GetLinkState().GetActiveLinks();
        const Vector initialPosition = topology.GetConstellation().GetPosition(3);
        Check(HasIncidentActiveLink(topology, 0) &&
                  HasIncidentActiveLink(topology, 1) &&
                  HasIncidentActiveLink(topology, 3),
              "fault task case requires initially connected target satellites");

        ComputeProfile profile;
        profile.nodes = {{3, 1000000000}, {5, 1000000000}};
        TaskTrace tasks;
        tasks.tasks = {
            MakeTask(1, 0, 3, 0, 40 * MILLISECOND_NS, 100000, 1, 1),
            MakeTask(2, 0, 3, 0, 30 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(3, 0, 3, 0, 20 * MILLISECOND_NS, 1, 1000000000, 1),
            MakeTask(4, 0, 3, 0, 10 * MILLISECOND_NS, 1, 1, 200000),
            MakeTask(5, 0, 3, 0, 0, 1, 1, 1),
            MakeTask(6, 0, 3, 0, 150 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(7, 0, 3, 0, 300 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(8, 0, 3, 0, 100 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(9, 0, 5, 2, 350 * MILLISECOND_NS, 100000, 1, 1),
            MakeTask(10, 0, 5, 2, 300 * MILLISECOND_NS, 1, 150000000, 1),
            MakeTask(11, 2, 5, 1, 500 * MILLISECOND_NS, 100000, 1, 1),
            MakeTask(12, 2, 5, 1, 450 * MILLISECOND_NS, 1, 1, 200000),
            MakeTask(13, 2, 5, 1, 600 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(14, 2, 5, 1, 700 * MILLISECOND_NS, 1, 1, 1),
            MakeTask(15, 2, 5, 1, 520 * MILLISECOND_NS, 1, 1000000000, 1),
        };

        FaultTrace trace;
        trace.faults = {
            MakeSatelliteFault(1,
                               3,
                               100 * MILLISECOND_NS,
                               150 * MILLISECOND_NS,
                               90 * MILLISECOND_NS,
                               0.9),
            MakeSatelliteFault(2,
                               0,
                               400 * MILLISECOND_NS,
                               100 * MILLISECOND_NS),
            MakeSatelliteFault(3,
                               1,
                               550 * MILLISECOND_NS,
                               100 * MILLISECOND_NS),
        };
        Ptr<FaultController> controller = CreateObject<FaultController>();
        controller->Configure(trace,
                              topology.GetIdMap().GetCanonicalSatelliteIds(),
                              TASK_SIMULATION_DURATION_NS);
        controller->BindTopology(topology);

        Ptr<TaskCoordinator> coordinator = CreateObject<TaskCoordinator>();
        coordinator->Initialize(profile,
                                tasks,
                                topology,
                                "fixed",
                                1024,
                                config.parameters.islMtuBytes,
                                config.parameters.receiverRcvBufBytes,
                                false,
                                TASK_SIMULATION_DURATION_NS);
        controller->BindTaskCoordinator(coordinator);

        bool firstStartObserved = false;
        bool firstRecoveryObserved = false;
        bool secondStartObserved = false;
        bool secondRecoveryObserved = false;
        bool thirdStartObserved = false;
        bool thirdRecoveryObserved = false;
        bool motionObserved = false;
        Simulator::Schedule(NanoSeconds(100 * MILLISECOND_NS), [&] {
            firstStartObserved = !HasIncidentActiveLink(topology, 3) &&
                                 topology.GetRouteComputationCount() == 2 &&
                                 !coordinator->IsSatelliteAvailable(3) &&
                                 !coordinator->IsComputeAvailable(3);
        });
        Simulator::Schedule(NanoSeconds(150 * MILLISECOND_NS), [&] {
            motionObserved =
                Distance(initialPosition, topology.GetConstellation().GetPosition(3)) > 1.0;
        });
        Simulator::Schedule(NanoSeconds(250 * MILLISECOND_NS), [&] {
            firstRecoveryObserved = HasIncidentActiveLink(topology, 3) &&
                                    topology.GetRouteComputationCount() == 3 &&
                                    coordinator->IsSatelliteAvailable(3) &&
                                    coordinator->IsComputeAvailable(3);
        });
        Simulator::Schedule(NanoSeconds(400 * MILLISECOND_NS), [&] {
            secondStartObserved = !HasIncidentActiveLink(topology, 0) &&
                                  topology.GetRouteComputationCount() == 4;
        });
        Simulator::Schedule(NanoSeconds(500 * MILLISECOND_NS), [&] {
            secondRecoveryObserved = HasIncidentActiveLink(topology, 0) &&
                                     topology.GetRouteComputationCount() == 5;
        });
        Simulator::Schedule(NanoSeconds(550 * MILLISECOND_NS), [&] {
            thirdStartObserved = !HasIncidentActiveLink(topology, 1) &&
                                 topology.GetRouteComputationCount() == 6;
        });
        Simulator::Schedule(NanoSeconds(650 * MILLISECOND_NS), [&] {
            thirdRecoveryObserved = HasIncidentActiveLink(topology, 1) &&
                                    topology.GetRouteComputationCount() == 7;
        });

        Simulator::Stop(NanoSeconds(TASK_SIMULATION_DURATION_NS));
        Simulator::Run();
        Check(firstStartObserved && firstRecoveryObserved && secondStartObserved &&
                  secondRecoveryObserved && thirdStartObserved &&
                  thirdRecoveryObserved,
              "satellite ISLs or routes did not change at exact fault timestamps");
        Check(motionObserved,
              "faulted satellite stopped moving in the natural orbit model");
        Check(topology.GetAppliedUpdateCount() == 1 &&
                  topology.GetRouteComputationCount() == 7 &&
                  topology.GetLinkState().GetActiveLinks() == initialLinks,
              "fault events waited for a network tick or did not restore fixed topology");

        const auto& faultEvents = controller->GetEvents();
        Check(faultEvents.size() == 7,
              "satellite notice/start/recovery event count differs");
        for (const FaultRuntimeEventRecord& event : faultEvents)
        {
            if (event.eventType != FaultEventType::NOTICE)
            {
                Check(event.routeRecomputed,
                      "edge-changing satellite event recorded no route recomputation");
            }
        }
        const FaultRuntimeEventRecord& firstStart =
            FindFaultEvent(*controller, 1, FaultEventType::START);
        const FaultRuntimeEventRecord& sourceStart =
            FindFaultEvent(*controller, 2, FaultEventType::START);
        const FaultRuntimeEventRecord& resultStart =
            FindFaultEvent(*controller, 3, FaultEventType::START);
        Check(!firstStart.satelliteAvailableAfter &&
                  !firstStart.communicationAvailableAfter &&
                  !firstStart.computeAvailableAfter &&
                  firstStart.affectedTaskCount == 5 &&
                  firstStart.affectedTransferCount == 7,
              "compute-satellite start availability or impact differs");
        Check(sourceStart.affectedTaskCount == 1 &&
                  sourceStart.affectedTransferCount == 2,
              "source-satellite fault impact differs");
        Check(resultStart.affectedTaskCount == 3 &&
                  resultStart.affectedTransferCount == 4,
              "result-satellite fault impact differs");
        Check(controller->GetState().GetActiveFaultIds().empty() &&
                  controller->GetState().GetCommunicationUnavailableNodeIds().empty(),
              "finite satellite faults remained active after recovery");

        for (const uint64_t taskId :
             std::vector<uint64_t>{1, 2, 3, 4, 6, 8})
        {
            const TaskRuntime& task = FindTask(*coordinator, taskId);
            Check(task.state == TASK_FAILED &&
                      task.failureReason ==
                          TaskFailureReason::COMPUTE_SATELLITE_FAILURE,
                  "compute-satellite task failure reason differs");
        }
        Check(FindTask(*coordinator, 9).failureReason ==
                  TaskFailureReason::SOURCE_SATELLITE_FAILURE,
              "source-satellite task failure reason differs");
        for (const uint64_t taskId : std::vector<uint64_t>{11, 12, 13, 15})
        {
            Check(FindTask(*coordinator, taskId).state == TASK_FAILED &&
                      FindTask(*coordinator, taskId).failureReason ==
                          TaskFailureReason::RESULT_SATELLITE_FAILURE,
                  "result-satellite task failure reason differs");
        }
        for (const uint64_t taskId : std::vector<uint64_t>{5, 7, 10, 14})
        {
            Check(FindTask(*coordinator, taskId).state == TASK_COMPLETED,
                  "unaffected or post-recovery satellite task did not complete");
        }
        Check(FindTask(*coordinator, 10).computeStartTimeNs <
                      400 * MILLISECOND_NS &&
                  FindTask(*coordinator, 10).resultTransferStartTimeNs >
                      400 * MILLISECOND_NS,
              "source ceased to be irrelevant after INPUT completion");

        Ptr<NetworkTransferEngine> engine = coordinator->GetTransferEngine();
        CheckTransferTerminal(engine,
                              1,
                              TransferRuntimeState::FAILED,
                              TransferTerminalReason::DESTINATION_SATELLITE_FAILED);
        CheckTransferTerminal(engine,
                              8,
                              TransferRuntimeState::FAILED,
                              TransferTerminalReason::SOURCE_SATELLITE_FAILED);
        CheckTransferTerminal(engine,
                              17,
                              TransferRuntimeState::FAILED,
                              TransferTerminalReason::SOURCE_SATELLITE_FAILED);
        CheckTransferTerminal(engine,
                              21,
                              TransferRuntimeState::CANCELLED,
                              TransferTerminalReason::TASK_FAILED);
        CheckTransferTerminal(engine,
                              24,
                              TransferRuntimeState::FAILED,
                              TransferTerminalReason::DESTINATION_SATELLITE_FAILED);
        for (const uint64_t transferId :
             std::vector<uint64_t>{2, 4, 6, 11, 12, 15, 16, 18, 22, 25, 26})
        {
            CheckTransferTerminal(engine,
                                  transferId,
                                  TransferRuntimeState::CANCELLED,
                                  TransferTerminalReason::TASK_FAILED);
        }
        CheckTransferTerminal(engine,
                              30,
                              TransferRuntimeState::CANCELLED,
                              TransferTerminalReason::TASK_FAILED);
        Check(engine->GetTransferState(29) == TransferRuntimeState::COMPLETED,
              "result-endpoint running task lost its completed INPUT history");
        for (const uint64_t transferId :
             std::vector<uint64_t>{9, 10, 13, 14, 19, 20, 27, 28})
        {
            Check(engine->GetTransferState(transferId) ==
                      TransferRuntimeState::COMPLETED,
                  "unaffected satellite transfer did not complete");
        }

        const auto& services = coordinator->GetComputeServices();
        const auto node3 = std::find_if(services.begin(), services.end(), [](const auto& service) {
            return service->GetNodeId() == 3;
        });
        const auto node5 = std::find_if(services.begin(), services.end(), [](const auto& service) {
            return service->GetNodeId() == 5;
        });
        Check(node3 != services.end() && node5 != services.end(),
              "satellite task case has no expected ComputeService");
        Check((*node3)->IsIdle() && (*node3)->GetCompletedTaskCount() == 3 &&
                  (*node3)->GetCancelledRunningTaskCount() == 1 &&
                  (*node3)->GetRemovedQueuedTaskCount() == 1,
              "compute-satellite service cleanup differs");
        Check((*node5)->IsIdle() && (*node5)->GetCompletedTaskCount() == 3 &&
                  (*node5)->GetCancelledRunningTaskCount() == 1 &&
                  (*node5)->GetRemovedQueuedTaskCount() == 0,
              "source/result satellite compute cleanup differs");
    }
    ResetSimulationGlobals();
}

struct PathFixture
{
    uint32_t source{};
    uint32_t destination{};
    uint32_t intermediate{};
};

PathFixture
FindCapacityPathFixture(OnlineTopologyController& topology)
{
    CapacityReservationState reservations;
    std::unique_ptr<PathPolicy> policy = RoutingPolicyFactory::CreatePathPolicy(
        RoutingMode::CAPACITY_AWARE_HRW,
        &topology,
        &reservations);
    const uint32_t nodeCount = topology.GetIdMap().GetNodeCount();
    for (uint32_t source = 0; source < nodeCount; ++source)
    {
        for (uint32_t destination = 0; destination < nodeCount; ++destination)
        {
            if (source == destination)
            {
                continue;
            }
            EcmpFlowKey flowKey;
            flowKey.sourceAddress = topology.GetServiceAddress(source);
            flowKey.destinationAddress = topology.GetServiceAddress(destination);
            flowKey.protocol = 17;
            flowKey.sourcePort = NETWORK_TRANSFER_FIRST_SOURCE_PORT;
            flowKey.destinationPort = NETWORK_TRANSFER_DESTINATION_PORT;
            CapacityAwarePath path;
            const PathSelectionContext context = {flowKey,
                                                  source,
                                                  destination,
                                                  topology.GetHashSeed()};
            if (policy->FindPath(context, path) && path.hops.size() >= 2)
            {
                return {source, destination, path.hops.front().destinationSatelliteId};
            }
        }
    }
    throw std::runtime_error("could not find a capacity path with an intermediate node");
}

void
RunCapacityMiddleNodeCase()
{
    constexpr int64_t durationNs = 300 * MILLISECOND_NS;
    OnlineTestConfiguration config = MakeOnlineTestConfig(3,
                                                           8,
                                                           "fixed",
                                                           durationNs,
                                                           1000 * MILLISECOND_NS,
                                                           6171353.0L);
    config.parameters.fixedDelaySeconds = 0.00001;
    config.parameters.islBandwidthBps = 10000000;
    config.parameters.routingMode = "global-capacity-aware-hrw";
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const PathFixture fixture = FindCapacityPathFixture(topology);
        Check(fixture.intermediate != fixture.source &&
                  fixture.intermediate != fixture.destination,
              "capacity fault target is not an intermediate node");

        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        engine->Configure(topology,
                          "fixed",
                          1024,
                          config.parameters.islMtuBytes,
                          config.parameters.receiverRcvBufBytes,
                          false,
                          durationNs);
        NetworkTransfer transfer;
        transfer.transferId = 1;
        transfer.sourceSatelliteId = fixture.source;
        transfer.destinationSatelliteId = fixture.destination;
        transfer.sizeBytes = 5000000;
        transfer.arrivalTimeNs = 1 * MILLISECOND_NS;
        engine->RegisterPlans({transfer});

        FaultTrace trace;
        trace.faults = {
            MakeSatelliteFault(1,
                               fixture.intermediate,
                               30 * MILLISECOND_NS,
                               30 * MILLISECOND_NS),
        };
        Ptr<FaultController> controller = CreateObject<FaultController>();
        controller->Configure(trace,
                              topology.GetIdMap().GetCanonicalSatelliteIds(),
                              durationNs);
        controller->BindTopology(topology);

        bool startReAdmissionObserved = false;
        bool recoveryReAdmissionObserved = false;
        Simulator::Schedule(NanoSeconds(1 * MILLISECOND_NS), [engine] {
            engine->StartTransferNow(1);
        });
        Simulator::Schedule(NanoSeconds(30 * MILLISECOND_NS), [&] {
            const CapacityAwareRuntimeSummary summary =
                engine->CollectCapacityAwareSummary();
            startReAdmissionObserved = !engine->IsTerminal(1) &&
                                       engine->GetTransferState(1) ==
                                           TransferRuntimeState::ACTIVE &&
                                       summary.activePathCountAtEnd == 1 &&
                                       summary.pendingTransferCountAtEnd == 0 &&
                                       topology.GetRouteComputationCount() == 2;
        });
        Simulator::Schedule(NanoSeconds(60 * MILLISECOND_NS), [&] {
            const CapacityAwareRuntimeSummary summary =
                engine->CollectCapacityAwareSummary();
            recoveryReAdmissionObserved = !engine->IsTerminal(1) &&
                                          summary.activePathCountAtEnd == 1 &&
                                          topology.GetRouteComputationCount() == 3;
        });
        bool cleanupSucceeded = false;
        Simulator::Schedule(NanoSeconds(250 * MILLISECOND_NS), [&] {
            cleanupSucceeded = engine->FinalizeTransferIfActive(
                1,
                TransferTerminalState::CANCELLED,
                TransferTerminalReason::SIMULATION_ENDED);
        });
        Simulator::Stop(NanoSeconds(durationNs));
        Simulator::Run();

        Check(startReAdmissionObserved && recoveryReAdmissionObserved,
              "capacity-aware middle-node path was not synchronously re-admitted");
        Check(cleanupSucceeded &&
                  engine->GetTerminalReason(1) ==
                      TransferTerminalReason::SIMULATION_ENDED,
              "middle-node fault incorrectly terminated the transfer");
        Ptr<FlowRouteRegistry> registry = topology.GetFlowRouteRegistry();
        const auto& events = registry->GetEvents();
        Check(std::any_of(events.begin(), events.end(), [](const auto& event) {
                  return event.simulationTimeNs == 30 * MILLISECOND_NS &&
                         event.action == "RELEASE_ROUTE_INVALIDATED";
              }),
              "capacity-aware path emitted no fault-time invalidation release");
        for (const FlowRouteReservationEvent& event : events)
        {
            if (event.simulationTimeNs != 30 * MILLISECOND_NS ||
                event.action != "ASSIGN")
            {
                continue;
            }
            const uint32_t nextHop = topology.GetNextHopSatelliteId(
                event.nodeId,
                event.candidate.outputInterface);
            Check(event.nodeId != fixture.intermediate &&
                      nextHop != fixture.intermediate,
                  "capacity-aware replacement path still traversed the failed satellite");
        }
        const CapacityAwareRuntimeSummary finalSummary =
            engine->CollectCapacityAwareSummary();
        Check(finalSummary.activePathCountAtEnd == 0 &&
                  finalSummary.reservedDirectedLinkCountAtEnd == 0 &&
                  finalSummary.totalReservedRateBpsAtEnd == 0 &&
                  finalSummary.pendingTransferCountAtEnd == 0 &&
                  registry->GetActiveFlowCount() == 0 &&
                  registry->GetAssignmentCount() == 0 &&
                  registry->GetTotalReservedBytes() == 0,
              "capacity-aware fault path leaked reservation state");
        Check(FindFaultEvent(*controller, 1, FaultEventType::START).routeRecomputed &&
                  FindFaultEvent(*controller, 1, FaultEventType::RECOVERY).routeRecomputed,
              "middle-node fault batch did not record its route updates");
    }
    ResetSimulationGlobals();
}

struct DistanceGateFixture
{
    uint32_t first{};
    uint32_t second{};
    double maximumDistanceM{};
};

DistanceGateFixture
FindIncreasingCandidateDistance(const ConstellationDefinition& constellation,
                                int64_t recoveryTimeNs)
{
    DistanceGateFixture fixture;
    double bestIncrease = 0.0;
    {
        OnlineOrbitConstellation orbit(constellation);
        const auto candidates =
            BuildPlusGridCandidateLinks(constellation, orbit.GetPositions());
        const auto initial = orbit.GetPositions();
        std::vector<SatelliteEcefPosition> future;
        Simulator::Schedule(NanoSeconds(recoveryTimeNs), [&] {
            future = orbit.GetPositions();
        });
        Simulator::Stop(NanoSeconds(recoveryTimeNs));
        Simulator::Run();
        for (const PlusGridCandidateLink& candidate : candidates)
        {
            const double initialDistance =
                Distance(initial[candidate.sourceId].positionM,
                         initial[candidate.destinationId].positionM);
            const double futureDistance =
                Distance(future[candidate.sourceId].positionM,
                         future[candidate.destinationId].positionM);
            const double increase = futureDistance - initialDistance;
            if (increase > bestIncrease)
            {
                bestIncrease = increase;
                fixture = {candidate.sourceId,
                           candidate.destinationId,
                           (initialDistance + futureDistance) / 2.0};
            }
        }
    }
    ResetSimulationGlobals();
    Check(bestIncrease > 1.0,
          "could not find a candidate whose natural distance increases");
    return fixture;
}

void
RunRecoveryDistanceGateCase()
{
    constexpr int64_t recoveryTimeNs = 100000000000LL;
    constexpr int64_t durationNs = 110000000000LL;
    const ConstellationDefinition constellation = MakeTestConstellation(3, 4);
    const DistanceGateFixture fixture =
        FindIncreasingCandidateDistance(constellation, recoveryTimeNs);
    OnlineTestConfiguration config = MakeOnlineTestConfig(3,
                                                           4,
                                                           "distance",
                                                           durationNs,
                                                           200000000000LL,
                                                           fixture.maximumDistanceM);
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        Check(topology.GetLinkState().IsLinkActive(fixture.first, fixture.second),
              "distance-gate candidate was not naturally active at time zero");

        FaultTrace trace;
        trace.faults = {MakeSatelliteFault(1,
                                           fixture.first,
                                           1000000000LL,
                                           recoveryTimeNs - 1000000000LL)};
        Ptr<FaultController> controller = CreateObject<FaultController>();
        controller->Configure(trace,
                              topology.GetIdMap().GetCanonicalSatelliteIds(),
                              durationNs);
        controller->BindTopology(topology);

        bool stayedDownAtRecovery = false;
        Simulator::Schedule(NanoSeconds(recoveryTimeNs), [&] {
            const auto evaluated = std::find_if(
                topology.GetLastTopologyState().evaluatedLinks.begin(),
                topology.GetLastTopologyState().evaluatedLinks.end(),
                [&](const EvaluatedSatelliteLink& link) {
                    return link.sourceId == fixture.first &&
                           link.destinationId == fixture.second;
                });
            stayedDownAtRecovery =
                evaluated != topology.GetLastTopologyState().evaluatedLinks.end() &&
                !evaluated->active &&
                !topology.GetLinkState().IsLinkActive(fixture.first, fixture.second) &&
                controller->GetState().IsSatelliteAvailable(fixture.first);
        });
        Simulator::Stop(NanoSeconds(durationNs));
        Simulator::Run();
        Check(stayedDownAtRecovery,
              "recovery restored a link that was no longer naturally distance-valid");
        Check(topology.GetAppliedUpdateCount() == 1,
              "distance-gate recovery relied on a periodic network update");
    }
    ResetSimulationGlobals();
}

void
RunPermanentSatelliteFaultCase()
{
    constexpr int64_t durationNs = 100 * MILLISECOND_NS;
    OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                           8,
                                                           "fixed",
                                                           durationNs,
                                                           1000 * MILLISECOND_NS,
                                                           6171353.0L);
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        FaultDefinition firstPermanent =
            MakeSatelliteFault(1, 3, 10 * MILLISECOND_NS, 1);
        firstPermanent.durationNs.reset();
        FaultDefinition secondPermanent =
            MakeSatelliteFault(2, 4, 10 * MILLISECOND_NS, 1);
        secondPermanent.durationNs.reset();
        FaultTrace trace;
        trace.faults = {firstPermanent, secondPermanent};
        Ptr<FaultController> controller = CreateObject<FaultController>();
        controller->Configure(trace,
                              topology.GetIdMap().GetCanonicalSatelliteIds(),
                              durationNs);
        controller->BindTopology(topology);
        Simulator::Stop(NanoSeconds(durationNs));
        Simulator::Run();
        Check(controller->GetEvents().size() == 2 &&
                  controller->GetEvents()[0].eventType == FaultEventType::START &&
                  controller->GetEvents()[1].eventType == FaultEventType::START &&
                  !controller->GetEvents()[0].routeRecomputed &&
                  controller->GetEvents()[1].routeRecomputed &&
                  controller->GetState().GetActiveFaultIds() ==
                      std::set<uint64_t>({1, 2}) &&
                  !controller->GetState().IsSatelliteAvailable(3) &&
                  !controller->GetState().IsSatelliteAvailable(4) &&
                  !HasIncidentActiveLink(topology, 3) &&
                  !HasIncidentActiveLink(topology, 4) &&
                  topology.GetRouteComputationCount() == 2,
              "permanent satellite batch recovered or recomputed routes more than once");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main()
{
    try
    {
        RunTaskAndTopologyCase();
        RunCapacityMiddleNodeCase();
        RunRecoveryDistanceGateCase();
        RunPermanentSatelliteFaultCase();
        std::cout << "SatCompute satellite fault execution tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
