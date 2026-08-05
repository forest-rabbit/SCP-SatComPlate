/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer-config.h"
#include "ns3/network-transfer.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"

#include "../support/config-factory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeDiamondReplayTestConfig;
using satcompute::test::MakeReplayTestConfig;

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

bool
SameTask(const TaskDefinition& left, const TaskDefinition& right)
{
    return left.taskId == right.taskId && left.sourceNodeId == right.sourceNodeId &&
           left.computeNodeId == right.computeNodeId &&
           left.resultNodeId == right.resultNodeId && left.inputBytes == right.inputBytes &&
           left.outputBytes == right.outputBytes &&
           left.computeWorkUnits == right.computeWorkUnits &&
           left.arrivalTimeNs == right.arrivalTimeNs &&
           left.inputTransferId == right.inputTransferId &&
           left.resultTransferId == right.resultTransferId;
}

ResolvedSatComputeConfig
MakeLegacyStaticConfig(const std::filesystem::path& topologyDirectory,
                       int64_t durationNs,
                       const std::string& routingMode)
{
    ResolvedSatComputeConfig config = MakeReplayTestConfig(topologyDirectory,
                                                           2,
                                                           2,
                                                           durationNs,
                                                           20000000000LL);
    config.routing.mode = routingMode;
    config.workloads.transferChunkMode = "fixed";
    config.workloads.transferPayloadBytes = 1024;
    config.network.islMtuBytes = 1500;
    config.network.islQueueBytes = 1500000;
    return config;
}

Ptr<TaskCoordinator>
CreateCoordinator(const std::filesystem::path& profileFilename,
                  const std::filesystem::path& taskFilename,
                  const ResolvedSatComputeConfig& config,
                  ReplayTopologyController& controller)
{
    controller.Initialize();
    const ComputeProfile profile = ReadComputeProfile(profileFilename, controller);
    const TaskTrace trace =
        ReadTaskTrace(taskFilename, config.simulation.durationNs, controller, profile);
    Ptr<TaskCoordinator> coordinator = CreateObject<TaskCoordinator>();
    coordinator->Initialize(profile,
                            trace,
                            controller,
                            config.workloads.transferChunkMode,
                            config.workloads.transferPayloadBytes,
                            config.network.islMtuBytes,
                            config.network.receiverRcvBufBytes,
                            true,
                            config.simulation.durationNs);
    return coordinator;
}

void
CheckCanonicalLegacyInputs(const std::filesystem::path& staticTopology,
                           const std::filesystem::path& computeRoot,
                           const std::filesystem::path& taskRoot,
                           const std::filesystem::path& transferRoot)
{
    {
        const ResolvedSatComputeConfig config =
            MakeLegacyStaticConfig(staticTopology, 3000000000LL, "global-first");
        ReplayTopologyController controller(config);
        controller.Initialize();

        const ComputeProfile first =
            ReadComputeProfile(computeRoot / "compute-profile-order-a.json", controller);
        const ComputeProfile second =
            ReadComputeProfile(computeRoot / "compute-profile-order-b.json", controller);
        Check(first.nodes.size() == 2 && second.nodes.size() == 2,
              "legacy compute profile count differs");
        Check(first.nodes[0].nodeId == 1 &&
                  first.nodes[0].computeRateWorkUnitsPerSecond == 2000000 &&
                  first.nodes[1].nodeId == 3 &&
                  first.nodes[1].computeRateWorkUnitsPerSecond == 1000000,
              "legacy compute profile golden values differ");
        for (std::size_t index = 0; index < first.nodes.size(); ++index)
        {
            Check(first.nodes[index].nodeId == second.nodes[index].nodeId &&
                      first.nodes[index].computeRateWorkUnitsPerSecond ==
                          second.nodes[index].computeRateWorkUnitsPerSecond,
                  "legacy compute profile array order changed canonical output");
        }

        const TaskTrace firstTrace = ReadTaskTrace(taskRoot / "task-order-a.json",
                                                   config.simulation.durationNs,
                                                   controller,
                                                   first);
        const TaskTrace secondTrace = ReadTaskTrace(taskRoot / "task-order-b.json",
                                                    config.simulation.durationNs,
                                                    controller,
                                                    first);
        Check(firstTrace.tasks.size() == 4 && secondTrace.tasks.size() == 4,
              "legacy canonical task count differs");
        for (std::size_t index = 0; index < firstTrace.tasks.size(); ++index)
        {
            Check(SameTask(firstTrace.tasks[index], secondTrace.tasks[index]),
                  "legacy task array order changed canonical output");
            const uint64_t taskId = index + 1;
            Check(firstTrace.tasks[index].taskId == taskId &&
                      firstTrace.tasks[index].inputTransferId == taskId * 2 - 1 &&
                      firstTrace.tasks[index].resultTransferId == taskId * 2,
                  "legacy task transfer-ID derivation differs");
        }

        const std::vector<NetworkTransfer> transfers = ReadNetworkTransferTrace(
            transferRoot / "diamond-4-static-transfers.json",
            config.simulation.durationNs,
            "fixed",
            1024,
            controller);
        Check(transfers.size() == 4, "legacy static transfer count differs");
        for (std::size_t index = 0; index < transfers.size(); ++index)
        {
            const NetworkTransfer& transfer = transfers[index];
            const EcmpFlowKey flowKey = BuildNetworkTransferFlowKey(transfer);
            Check(transfer.transferId == index + 1 && transfer.sourcePort == 10000 + index &&
                      transfer.destinationPort == 9000 && transfer.packetCount == 4 &&
                      transfer.finalPacketPayloadBytes == 1024 && flowKey.protocol == 17 &&
                      flowKey.sourcePort == transfer.sourcePort &&
                      flowKey.destinationPort == transfer.destinationPort,
                  "legacy transfer packetization or five-tuple differs");
        }
    }
    ResetSimulationGlobals();
}

void
RunLegacySingleTask(const std::filesystem::path& staticTopology,
                    const std::filesystem::path& computeRoot,
                    const std::filesystem::path& taskRoot)
{
    {
        const ResolvedSatComputeConfig config =
            MakeLegacyStaticConfig(staticTopology, 3000000000LL, "global-hash-per-flow");
        ReplayTopologyController controller(config);
        Ptr<TaskCoordinator> coordinator = CreateCoordinator(
            computeRoot / "diamond-4-compute-profile.json",
            taskRoot / "task-single-ecmp.json",
            config,
            controller);
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(coordinator->IsComplete(), "legacy single task did not complete");
        coordinator->ValidateCompleted();
        const TaskRuntime& task = coordinator->GetTaskRuntimes().front();
        Check(task.computeCompleteTimeNs - task.computeStartTimeNs == 1000000000LL,
              "legacy single-task compute duration differs");
        Check(coordinator->GetTaskEvents().size() == 5,
              "legacy single-task lifecycle event count differs");

        Ptr<NetworkTransferEngine> engine = coordinator->GetTransferEngine();
        const std::vector<NetworkTransfer>& plans = engine->GetPlans();
        Check(plans.size() == 2 && plans[0].transferId == 1 &&
                  plans[0].sizeBytes == 4096 && plans[0].packetCount == 4 &&
                  plans[1].transferId == 2 && plans[1].sizeBytes == 2050 &&
                  plans[1].packetCount == 3 && plans[1].finalPacketPayloadBytes == 2 &&
                  plans[1].arrivalTimeNs == task.computeCompleteTimeNs,
              "legacy task-derived transfer golden values differ");
        const ApplicationMetrics metrics = engine->CollectApplicationMetrics();
        Check(metrics.sinkApplications == 2 && metrics.sentBytes == 6146 &&
                  metrics.receivedBytes == 6146,
              "legacy single-task network totals differ");
    }
    ResetSimulationGlobals();
}

void
RunLegacyFcfsTasks(const std::filesystem::path& staticTopology,
                   const std::filesystem::path& computeRoot,
                   const std::filesystem::path& taskRoot)
{
    {
        const ResolvedSatComputeConfig config =
            MakeLegacyStaticConfig(staticTopology, 4000000000LL, "global-hash-per-flow");
        ReplayTopologyController controller(config);
        Ptr<TaskCoordinator> coordinator = CreateCoordinator(
            computeRoot / "diamond-4-compute-profile.json",
            taskRoot / "task-fcfs.json",
            config,
            controller);
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(coordinator->IsComplete(), "legacy FCFS tasks did not complete");
        const std::vector<TaskRuntime>& tasks = coordinator->GetTaskRuntimes();
        Check(tasks.size() == 3, "legacy FCFS task count differs");
        std::vector<const TaskRuntime*> expectedOrder;
        std::vector<const TaskRuntime*> actualOrder;
        for (const TaskRuntime& task : tasks)
        {
            expectedOrder.push_back(&task);
            actualOrder.push_back(&task);
        }
        std::sort(expectedOrder.begin(),
                  expectedOrder.end(),
                  [](const TaskRuntime* left, const TaskRuntime* right) {
                      if (left->queueEnterTimeNs != right->queueEnterTimeNs)
                      {
                          return left->queueEnterTimeNs < right->queueEnterTimeNs;
                      }
                      return left->definition.taskId < right->definition.taskId;
                  });
        std::sort(actualOrder.begin(),
                  actualOrder.end(),
                  [](const TaskRuntime* left, const TaskRuntime* right) {
                      return left->computeStartTimeNs < right->computeStartTimeNs;
                  });
        for (std::size_t index = 0; index < tasks.size(); ++index)
        {
            Check(actualOrder[index]->definition.taskId ==
                      expectedOrder[index]->definition.taskId,
                  "legacy task dispatch violates deterministic FCFS order");
            const int64_t expectedStart =
                index == 0
                    ? actualOrder[index]->queueEnterTimeNs
                    : std::max(actualOrder[index]->queueEnterTimeNs,
                               actualOrder[index - 1]->computeCompleteTimeNs);
            Check(actualOrder[index]->computeStartTimeNs == expectedStart,
                  "legacy FCFS schedule contains a gap or overlap");
        }
        const std::array<int64_t, 3> serviceTimes = {
            tasks[0].computeCompleteTimeNs - tasks[0].computeStartTimeNs,
            tasks[1].computeCompleteTimeNs - tasks[1].computeStartTimeNs,
            tasks[2].computeCompleteTimeNs - tasks[2].computeStartTimeNs,
        };
        Check(serviceTimes == std::array<int64_t, 3>{500000000, 750000000, 250000000},
              "legacy FCFS service-time goldens differ");
        const Ptr<ComputeService>& service = coordinator->GetComputeServices().front();
        Check(service->GetEnqueuedTaskCount() == 3 &&
                  service->GetCompletedTaskCount() == 3 &&
                  service->GetBusyTimeNs() == 1500000000LL,
              "legacy FCFS compute summary differs");
    }
    ResetSimulationGlobals();
}

void
RunLegacyHeterogeneousTasks(const std::filesystem::path& staticTopology,
                            const std::filesystem::path& computeRoot,
                            const std::filesystem::path& taskRoot)
{
    {
        const ResolvedSatComputeConfig config =
            MakeLegacyStaticConfig(staticTopology, 4000000000LL, "global-first");
        ReplayTopologyController controller(config);
        Ptr<TaskCoordinator> coordinator = CreateCoordinator(
            computeRoot / "heterogeneous-compute-profile.json",
            taskRoot / "task-heterogeneous.json",
            config,
            controller);
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(coordinator->IsComplete(), "legacy heterogeneous tasks did not complete");
        const std::vector<TaskRuntime>& tasks = coordinator->GetTaskRuntimes();
        Check(tasks.size() == 2 && tasks[0].definition.computeNodeId == 3 &&
                  tasks[0].computeCompleteTimeNs - tasks[0].computeStartTimeNs ==
                      2000000000LL &&
                  tasks[1].definition.computeNodeId == 1 &&
                  tasks[1].computeCompleteTimeNs - tasks[1].computeStartTimeNs ==
                      1000000000LL,
              "legacy heterogeneous compute-rate behavior differs");
        Check(coordinator->GetComputeServices().size() == 2,
              "legacy heterogeneous profile did not create two services");
    }
    ResetSimulationGlobals();
}

void
RunLegacyTransfersOnDynamicTopology(const std::filesystem::path& dynamicTopology,
                                    const std::filesystem::path& transferRoot)
{
    {
        ResolvedSatComputeConfig config = MakeDiamondReplayTestConfig(dynamicTopology);
        config.routing.mode = "global-hash-per-flow";
        config.workloads.transferChunkMode = "fixed";
        config.workloads.transferPayloadBytes = 1024;
        ReplayTopologyController controller(config);
        controller.Initialize();
        const NetworkTransferState state = InstallNetworkTransfersNs(
            transferRoot / "diamond-4-dynamic-transfers.json",
            "fixed",
            1024,
            config.network.islMtuBytes,
            config.network.receiverRcvBufBytes,
            true,
            "silent",
            config.simulation.durationNs,
            controller);

        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        Check(state.engine->AreAllTransfersCompleted(),
              "legacy transfers did not complete across dynamic topology epochs");
        const ApplicationMetrics metrics = CollectNetworkTransferMetrics(state);
        Check(metrics.sinkApplications == 1 && metrics.sentBytes == 49152 &&
                  metrics.receivedBytes == 49152,
              "legacy dynamic transfer totals differ");
        const std::vector<TransferSummaryRecord> summaries =
            CollectNetworkTransferSummaries(state);
        Check(summaries.size() == 12 && summaries.front().transferId == 1 &&
                  summaries.back().transferId == 12,
              "legacy dynamic transfer summary order differs");
        for (const TransferSummaryRecord& summary : summaries)
        {
            Check(summary.derivedPacketCount == 4 && summary.sentPacketCount == 4 &&
                      summary.receivedPacketCount == 4 &&
                      summary.transferState == "COMPLETED",
                  "legacy dynamic transfer packet golden differs");
        }
        Check(controller.GetAppliedTopologySliceCount() == 3 &&
                  controller.GetRouteComputationCount() == 3,
              "dynamic topology did not preserve edge-change route epochs");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string staticTopology;
    std::string dynamicTopology;
    std::string computeRoot;
    std::string taskRoot;
    std::string transferRoot;
    CommandLine command(__FILE__);
    command.AddValue("staticTopology", "Legacy static diamond topology", staticTopology);
    command.AddValue("dynamicTopology", "Regular dynamic diamond topology", dynamicTopology);
    command.AddValue("computeRoot", "Legacy compute-profile fixture root", computeRoot);
    command.AddValue("taskRoot", "Legacy task fixture root", taskRoot);
    command.AddValue("transferRoot", "Legacy transfer fixture root", transferRoot);
    command.Parse(argc, argv);

    try
    {
        Check(!staticTopology.empty() && !dynamicTopology.empty() && !computeRoot.empty() &&
                  !taskRoot.empty() && !transferRoot.empty(),
              "all legacy workload fixture paths are required");
        CheckCanonicalLegacyInputs(staticTopology, computeRoot, taskRoot, transferRoot);
        RunLegacySingleTask(staticTopology, computeRoot, taskRoot);
        RunLegacyFcfsTasks(staticTopology, computeRoot, taskRoot);
        RunLegacyHeterogeneousTasks(staticTopology, computeRoot, taskRoot);
        RunLegacyTransfersOnDynamicTopology(dynamicTopology, transferRoot);
        std::cout << "SatCompute legacy workload parity tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
