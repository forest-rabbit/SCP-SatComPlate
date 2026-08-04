/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/flow-route-registry.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

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

void
CheckLifecycle(const TaskCoordinator& coordinator, std::size_t expectedTasks)
{
    Check(coordinator.IsComplete(), "task coordinator did not complete every task");
    coordinator.ValidateCompleted();
    const std::vector<TaskRuntime>& tasks = coordinator.GetTaskRuntimes();
    Check(tasks.size() == expectedTasks &&
              coordinator.GetTaskEvents().size() == expectedTasks * 5,
          "task runtime or lifecycle-event count differs");
    for (const TaskRuntime& task : tasks)
    {
        Check(task.state == TASK_COMPLETED &&
                  task.inputTransferCompleteTimeNs >= task.definition.arrivalTimeNs &&
                  task.queueEnterTimeNs == task.inputTransferCompleteTimeNs &&
                  task.computeStartTimeNs >= task.queueEnterTimeNs &&
                  task.computeCompleteTimeNs >= task.computeStartTimeNs &&
                  task.resultTransferStartTimeNs == task.computeCompleteTimeNs &&
                  task.resultTransferCompleteTimeNs >= task.resultTransferStartTimeNs,
              "completed task lifecycle timestamps are inconsistent");

        std::vector<TaskState> states;
        for (const TaskEventRecord& event : coordinator.GetTaskEvents())
        {
            if (event.taskId == task.definition.taskId)
            {
                Check(event.simulationTimeNs >= task.definition.arrivalTimeNs &&
                          !event.cause.empty(),
                      "task event time or cause differs");
                states.push_back(event.toState);
            }
        }
        Check(states == std::vector<TaskState>({TASK_INPUT_TRANSFERRING,
                                                TASK_QUEUED,
                                                TASK_RUNNING,
                                                TASK_RESULT_TRANSFERRING,
                                                TASK_COMPLETED}),
              "task did not follow the six-state lifecycle");
    }
}

Ptr<TaskCoordinator>
CreateCoordinator(const std::filesystem::path& computeProfileFilename,
                  const std::filesystem::path& taskTraceFilename,
                  const ScenarioConfig& config,
                  ReplayTopologyController& controller)
{
    controller.Initialize();
    const ComputeProfile profile = ReadComputeProfile(computeProfileFilename, controller);
    const TaskTrace trace = ReadTaskTrace(taskTraceFilename,
                                          config.simulation.durationNs,
                                          controller,
                                          profile);
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
RunSingleTaskMode(const std::string& scenarioFilename,
                  const std::filesystem::path& fixtureRoot,
                  const std::string& routingMode)
{
    {
        ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        config.routing.mode = routingMode;
        ReplayTopologyController controller(config);
        Ptr<TaskCoordinator> coordinator =
            CreateCoordinator(fixtureRoot / "compute-profile-single.json",
                              fixtureRoot / "task-single.json",
                              config,
                              controller);
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        CheckLifecycle(*coordinator, 1);
        const TaskRuntime& task = coordinator->GetTaskRuntimes().front();
        Check(task.computeCompleteTimeNs - task.computeStartTimeNs == 200000000,
              "single-task exact compute duration differs");
        Ptr<NetworkTransferEngine> engine = coordinator->GetTransferEngine();
        const std::vector<NetworkTransfer>& plans = engine->GetPlans();
        Check(plans.size() == 2 && plans[0].transferId == 1 && plans[0].arrivalTimeNs == 100000000 &&
                  plans[1].transferId == 2 && plans[1].arrivalTimeNs == task.computeCompleteTimeNs,
              "task transfer plans or runtime result start differ");
        const ApplicationMetrics metrics = engine->CollectApplicationMetrics();
        Check(metrics.sinkApplications == 2 && metrics.sentBytes == 6146 &&
                  metrics.receivedBytes == 6146,
              "single-task network metrics differ");
        Check(engine->CollectSummaries().size() == 2 &&
                  engine->CollectUdpSocketDropEvents().empty(),
              "single-task transfer summaries or UDP drops differ");
        const Ptr<ComputeService>& service = coordinator->GetComputeServices().front();
        Check(service->GetEnqueuedTaskCount() == 1 &&
                  service->GetCompletedTaskCount() == 1 &&
                  service->GetBusyTimeNs() == 200000000,
              "single-task compute metrics differ");

        Ptr<FlowRouteRegistry> registry = controller.GetFlowRouteRegistry();
        if (registry != nullptr)
        {
            Check(registry->GetActiveFlowCount() == 0 &&
                      registry->GetAssignmentCount() == 0 &&
                      registry->GetTotalReservedBytes() == 0,
                  "completed task leaked routing state");
        }
        if (controller.IsCapacityAwareRouting())
        {
            const CapacityAwareRuntimeSummary summary = engine->CollectCapacityAwareSummary();
            Check(summary.activePathCountAtEnd == 0 &&
                      summary.reservedDirectedLinkCountAtEnd == 0 &&
                      summary.totalReservedRateBpsAtEnd == 0 &&
                      summary.pendingTransferCountAtEnd == 0,
                  "completed capacity-aware task leaked path state");
        }
    }
    ResetSimulationGlobals();
}

void
RunFcfsTasks(const std::string& scenarioFilename, const std::filesystem::path& fixtureRoot)
{
    {
        ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        config.routing.mode = "global-first";
        ReplayTopologyController controller(config);
        Ptr<TaskCoordinator> coordinator =
            CreateCoordinator(fixtureRoot / "compute-profile-single.json",
                              fixtureRoot / "task-fcfs.json",
                              config,
                              controller);
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        CheckLifecycle(*coordinator, 3);
        const std::vector<TaskRuntime>& tasks = coordinator->GetTaskRuntimes();
        Check(tasks[0].definition.taskId == 1 && tasks[1].definition.taskId == 2 &&
                  tasks[2].definition.taskId == 3 &&
                  tasks[0].computeStartTimeNs < tasks[1].computeStartTimeNs &&
                  tasks[1].computeStartTimeNs < tasks[2].computeStartTimeNs &&
                  tasks[1].computeStartTimeNs >= tasks[0].computeCompleteTimeNs &&
                  tasks[2].computeStartTimeNs >= tasks[1].computeCompleteTimeNs,
              "coordinator did not preserve deterministic non-preemptive FCFS order");
        Check(tasks[0].computeCompleteTimeNs - tasks[0].computeStartTimeNs == 200000000 &&
                  tasks[1].computeCompleteTimeNs - tasks[1].computeStartTimeNs == 100000000 &&
                  tasks[2].computeCompleteTimeNs - tasks[2].computeStartTimeNs == 50000000,
              "FCFS task service durations differ");
        const Ptr<ComputeService>& service = coordinator->GetComputeServices().front();
        Check(service->GetEnqueuedTaskCount() == 3 &&
                  service->GetCompletedTaskCount() == 3 &&
                  service->GetBusyTimeNs() == 350000000,
              "FCFS compute aggregate metrics differ");
    }
    ResetSimulationGlobals();
}

void
RunHeterogeneousTasks(const std::string& scenarioFilename,
                      const std::filesystem::path& fixtureRoot)
{
    {
        ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        config.routing.mode = "global-first";
        ReplayTopologyController controller(config);
        Ptr<TaskCoordinator> coordinator =
            CreateCoordinator(fixtureRoot / "compute-profile-order-a.json",
                              fixtureRoot / "task-heterogeneous.json",
                              config,
                              controller);
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        CheckLifecycle(*coordinator, 2);
        const std::vector<TaskRuntime>& tasks = coordinator->GetTaskRuntimes();
        Check(tasks[0].definition.computeNodeId == 3 &&
                  tasks[0].computeCompleteTimeNs - tasks[0].computeStartTimeNs == 200000000 &&
                  tasks[1].definition.computeNodeId == 1 &&
                  tasks[1].computeCompleteTimeNs - tasks[1].computeStartTimeNs == 100000000,
              "heterogeneous compute rates did not produce exact service durations");
        Check(coordinator->GetComputeServices().size() == 2,
              "heterogeneous profile did not create two compute services");
        for (const Ptr<ComputeService>& service : coordinator->GetComputeServices())
        {
            Check(service->GetEnqueuedTaskCount() == 1 &&
                      service->GetCompletedTaskCount() == 1,
                  "heterogeneous compute service task count differs");
        }
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenarioFilename;
    std::string fixtureRoot;
    CommandLine command(__FILE__);
    command.AddValue("scenario", "Dynamic diamond replay scenario", scenarioFilename);
    command.AddValue("fixtureRoot", "Compute/task fixture directory", fixtureRoot);
    command.Parse(argc, argv);

    try
    {
        Check(!scenarioFilename.empty() && !fixtureRoot.empty(),
              "scenario and fixture root are required");
        for (const char* mode : {"global-first",
                                 "global-hash-per-flow",
                                 "global-hrw-per-flow",
                                 "global-size-aware-hrw",
                                 "global-capacity-aware-hrw"})
        {
            try
            {
                RunSingleTaskMode(scenarioFilename, fixtureRoot, mode);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(std::string(mode) + ": " + error.what());
            }
        }
        RunFcfsTasks(scenarioFilename, fixtureRoot);
        RunHeterogeneousTasks(scenarioFilename, fixtureRoot);
        std::cout << "SatCompute task coordinator tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
