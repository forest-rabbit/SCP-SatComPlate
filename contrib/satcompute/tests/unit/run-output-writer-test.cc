/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer-config.h"
#include "ns3/network-transfer-engine.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/run-output-writer.h"
#include "ns3/scenario-config.h"
#include "ns3/sha256.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"

#include "../../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

using Json = nlohmann::json;

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

std::string
ReadText(const std::filesystem::path& filename)
{
    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("cannot read test output: " + filename.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

Json
ReadJson(const std::filesystem::path& filename)
{
    return Json::parse(ReadText(filename));
}

std::size_t
CountLines(const std::filesystem::path& filename)
{
    const std::string content = ReadText(filename);
    return std::count(content.begin(), content.end(), '\n');
}

void
RunCompleteTaskOutput(const std::string& scenarioFilename,
                      const std::filesystem::path& outputRoot)
{
    {
        const ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        const std::filesystem::path outputDirectory = outputRoot / "complete";
        const std::filesystem::path effectiveConfig =
            WriteEffectiveConfig(config, outputDirectory);
        ReplayTopologyController controller(config);
        controller.Initialize();
        const ComputeProfile profile =
            ReadComputeProfile(*config.workloads.computeProfile, controller);
        const TaskTrace trace = ReadTaskTrace(*config.workloads.taskTrace,
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
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        coordinator->ValidateCompleted();

        const RunOutputContext context = {effectiveConfig,
                                          outputDirectory,
                                          123456,
                                          controller.GetAppliedSnapshotCount(),
                                          controller.GetRouteComputationCount(),
                                          controller.GetFlowRouteRegistry(),
                                          std::nullopt};
        Ptr<NetworkTransferEngine> engine = coordinator->GetTransferEngine();
        const RunOutputResult result = WriteRunOutputs(config,
                                                       context,
                                                       engine,
                                                       coordinator);
        Check(result.complete && !result.diagnosticsGenerated && result.files.size() == 8,
              "complete task output result differs");
        Check(!std::filesystem::exists(outputDirectory / "diagnostics"),
              "complete run unexpectedly generated diagnostics");
        Check(CountLines(outputDirectory / "transfer-summary.csv") == 3 &&
                  CountLines(outputDirectory / "task-events.csv") == 6 &&
                  CountLines(outputDirectory / "task-summary.csv") == 2 &&
                  CountLines(outputDirectory / "compute-node-summary.csv") == 2 &&
                  CountLines(outputDirectory / "routing-reservation-events.csv") > 1,
              "complete CSV row counts differ");

        const Json summary = ReadJson(result.runSummaryPath);
        Check(summary.at("run_status") == "COMPLETE" &&
                  summary.at("workload_mode") == "task" &&
                  summary.at("wall_clock_ns") == 123456 &&
                  summary.at("effective_config").at("sha256") ==
                      Sha256File(effectiveConfig) &&
                  summary.at("transfer").at("transfer_count") == 2 &&
                  summary.at("transfer").at("completed_transfer_count") == 2 &&
                  summary.at("transfer").at("declared_application_bytes") == 6146 &&
                  summary.at("transfer").at("received_application_bytes") == 6146 &&
                  summary.at("task").at("task_count") == 1 &&
                  summary.at("task").at("completed_task_count") == 1 &&
                  summary.at("route_computation_count") == 3,
              "complete run summary fields differ");
        const Json routing = ReadJson(outputDirectory / "routing-summary.json");
        Check(routing.at("routing_mode") == "global-size-aware-hrw" &&
                  routing.at("flow_registry").at("registered_flow_count") == 2 &&
                  routing.at("flow_registry").at("active_flow_count_at_end") == 0 &&
                  routing.at("flow_registry").at("assignment_count_at_end") == 0 &&
                  routing.at("flow_registry").at("peak_reserved_bytes").get<uint64_t>() > 0,
              "reservation-aware routing summary differs");

        const std::string firstSummary = ReadText(result.runSummaryPath);
        const std::string firstTransfers = ReadText(outputDirectory / "transfer-summary.csv");
        const RunOutputResult repeated = WriteRunOutputs(config,
                                                         context,
                                                         engine,
                                                         coordinator);
        Check(ReadText(repeated.runSummaryPath) == firstSummary &&
                  ReadText(outputDirectory / "transfer-summary.csv") == firstTransfers,
              "repeated output publication was not byte deterministic");
    }
    ResetSimulationGlobals();
}

void
RunPartialTransferOutput(const std::string& scenarioFilename,
                         const std::filesystem::path& outputRoot)
{
    {
        const ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        const std::filesystem::path outputDirectory = outputRoot / "partial";
        const std::filesystem::path effectiveConfig =
            WriteEffectiveConfig(config, outputDirectory);
        ReplayTopologyController controller(config);
        controller.Initialize();
        std::vector<NetworkTransfer> plans =
            ReadNetworkTransferTrace(*config.workloads.transferTrace,
                                     config.simulation.durationNs,
                                     config.workloads.transferChunkMode,
                                     config.workloads.transferPayloadBytes,
                                     controller);
        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        engine->Configure(controller,
                          config.workloads.transferChunkMode,
                          config.workloads.transferPayloadBytes,
                          config.network.islMtuBytes,
                          config.network.receiverRcvBufBytes,
                          true,
                          config.simulation.durationNs);
        engine->RegisterPlans(std::move(plans));
        engine->ScheduleDeclaredTransfers();
        Simulator::Stop(NanoSeconds(101000000));
        Simulator::Run();
        Check(!engine->AreAllTransfersCompleted(),
              "partial-output fixture unexpectedly completed");

        const RunOutputContext context = {effectiveConfig,
                                          outputDirectory,
                                          999,
                                          controller.GetAppliedSnapshotCount(),
                                          controller.GetRouteComputationCount(),
                                          controller.GetFlowRouteRegistry(),
                                          std::nullopt};
        const RunOutputResult result = WriteRunOutputs(config, context, engine, nullptr);
        Check(!result.complete && result.diagnosticsGenerated,
              "partial transfer output status differs");
        Check(CountLines(outputDirectory / "diagnostics/incomplete-transfers.csv") == 3 &&
                  CountLines(outputDirectory / "diagnostics/incomplete-tasks.csv") == 1,
              "partial diagnostic row counts differ");
        const Json summary = ReadJson(result.runSummaryPath);
        const Json diagnostic =
            ReadJson(outputDirectory / "diagnostics/diagnostic-summary.json");
        Check(summary.at("run_status") == "PARTIAL" &&
                  summary.at("task_completion_policy") == "strict" &&
                  summary.at("diagnostics_generated") == true &&
                  summary.at("transfer").at("transfer_count") == 2 &&
                  summary.at("transfer").at("completed_transfer_count") == 0 &&
                  diagnostic.at("incomplete_transfer_count") == 2 &&
                  diagnostic.at("incomplete_task_count") == 0,
              "partial run or diagnostic summary differs");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string taskScenario;
    std::string transferScenario;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("taskScenario", "Task replay scenario", taskScenario);
    command.AddValue("transferScenario", "Direct-transfer replay scenario", transferScenario);
    command.AddValue("outputDir", "Temporary output root", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!taskScenario.empty() && !transferScenario.empty() && !outputDirectory.empty(),
              "scenario and output paths are required");
        RunCompleteTaskOutput(taskScenario, outputDirectory);
        RunPartialTransferOutput(transferScenario, outputDirectory);
        std::cout << "SatCompute structured run output tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
