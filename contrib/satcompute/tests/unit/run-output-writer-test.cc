/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/effective-config.h"
#include "ns3/ecmp-route-recorder.h"
#include "ns3/flow-metrics.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer-config.h"
#include "ns3/network-transfer-engine.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/run-output-writer.h"
#include "ns3/sha256.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"

#include "../support/config-factory.h"
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
using satcompute::test::MakeDiamondReplayTestConfig;

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
    ResetSimulationFlowMonitor();
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

std::string
FirstLine(const std::filesystem::path& filename)
{
    std::istringstream input(ReadText(filename));
    std::string line;
    std::getline(input, line);
    return line;
}

void
WriteFixtureFile(const std::filesystem::path& filename, const std::string& content)
{
    std::filesystem::create_directories(filename.parent_path());
    std::ofstream output(filename, std::ios::out | std::ios::trunc);
    Check(output.is_open(), "cannot seed stale diagnostic fixture");
    output << content;
}

void
RunCompleteTaskOutput(const std::filesystem::path& constellationConfig,
                      const std::filesystem::path& topologyDirectory,
                      const std::filesystem::path& fixtureRoot,
                      const std::filesystem::path& outputRoot)
{
    {
        const std::filesystem::path outputDirectory = outputRoot / "complete";
        ResolvedSatComputeConfig config =
            MakeDiamondReplayTestConfig(topologyDirectory);
        config.runName = "task-replay-fixture";
        config.constellation = LoadConstellationDefinition(constellationConfig);
        config.routing.mode = "global-size-aware-hrw";
        config.workloads.computeProfile = fixtureRoot / "compute-profile-single.json";
        config.workloads.taskTrace = fixtureRoot / "task-single.json";
        config.outputDirectory = outputDirectory;
        const std::filesystem::path effectiveConfig = WriteEffectiveConfig(config);
        ReplayTopologyController controller(config);
        controller.Initialize();
        EcmpRouteRecorder routeRecorder(controller);
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
        const Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        coordinator->ValidateCompleted();

        WriteFixtureFile(outputDirectory / "udp-socket-drops.csv", "stale\n");
        WriteFixtureFile(outputDirectory / "diagnostics/diagnostic-summary.json", "{}\n");
        WriteFixtureFile(outputDirectory / "diagnostics/failure/incomplete-tasks.csv",
                         "stale\n");

        const RunOutputContext context = {effectiveConfig,
                                          outputDirectory,
                                          123456,
                                          controller.GetAppliedSnapshotCount(),
                                          controller.GetRouteComputationCount(),
                                          controller.GetFlowRouteRegistry(),
                                          std::nullopt,
                                          flowMonitor,
                                          routeRecorder.GetEvents(),
                                          controller.GetLinkState().GetDirectedLinks(),
                                          controller.GetLinkState().GetQueueDropEvents()};
        Ptr<NetworkTransferEngine> engine = coordinator->GetTransferEngine();
        const RunOutputResult result = WriteRunOutputs(config,
                                                       context,
                                                       engine,
                                                       coordinator);
        Check(result.complete && !result.diagnosticsGenerated && result.files.size() == 12,
              "complete task output result differs");
        Check(!std::filesystem::exists(outputDirectory / "diagnostics"),
              "complete run unexpectedly generated diagnostics");
        Check(CountLines(outputDirectory / "transfer-summary.csv") == 3 &&
                  CountLines(outputDirectory / "network-flow-metrics.csv") == 2 &&
                  CountLines(outputDirectory / "network-flow-details.csv") == 3 &&
                  CountLines(outputDirectory / "ecmp-route-events.csv") > 1 &&
                  CountLines(outputDirectory / "size-aware-reservation-events.csv") > 1 &&
                  CountLines(outputDirectory / "task-events.csv") == 6 &&
                  CountLines(outputDirectory / "task-summary.csv") == 2 &&
                  CountLines(outputDirectory / "compute-node-summary.csv") == 2 &&
                  CountLines(outputDirectory / "routing-reservation-events.csv") > 1,
              "complete CSV row counts differ");
        Check(FirstLine(outputDirectory / "transfer-summary.csv") ==
                      "transfer_id,source_node_id,destination_node_id,source_address,"
                      "destination_address,source_port,destination_port,declared_size_bytes,"
                      "effective_payload_bytes,pacing_mode,derived_packet_count,"
                      "final_packet_payload_bytes,arrival_time_ns,last_send_time_ns,"
                      "sent_application_bytes,received_application_bytes,"
                      "received_packet_count,completion_time_ns,completion_delay_ns",
              "legacy transfer summary header differs");

        const Json summary = ReadJson(result.runSummaryPath);
        Check(summary.at("run_name") == "task-replay-fixture" &&
                  summary.at("config_schema_version") == "0.3" &&
                  summary.at("run_status") == "COMPLETE" &&
                  summary.at("mode") == "task" &&
                  summary.at("simulation_duration_s") == 5.0 &&
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
                  summary.at("flow_monitor_tx_packets") == 7 &&
                  summary.at("flow_monitor_rx_packets") == 7 &&
                  summary.at("flow_monitor_lost_packets") == 0 &&
                  summary.at("route_computation_count") == 3,
              "complete run summary fields differ");
        const Json routing = ReadJson(outputDirectory / "routing-summary.json");
        Check(routing.at("routing_mode") == "global-size-aware-hrw" &&
                  routing.at("flow_registry").at("registered_flow_count") == 2 &&
                  routing.at("flow_registry").at("active_flow_count_at_end") == 0 &&
                  routing.at("flow_registry").at("assignment_count_at_end") == 0 &&
                  routing.at("flow_registry").at("peak_reserved_bytes").get<uint64_t>() > 0,
              "reservation-aware routing summary differs");
        const Json sizeAware = ReadJson(outputDirectory / "size-aware-summary.json");
        Check(sizeAware.at("registered_flow_count") == 2 &&
                  sizeAware.at("active_flow_count_at_end") == 0 &&
                  sizeAware.at("assignment_count_at_end") == 0 &&
                  sizeAware.at("reservation_event_count").get<uint64_t>() > 0 &&
                  sizeAware.at("peak_total_reserved_bytes").get<uint64_t>() > 0,
              "legacy size-aware summary differs");

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
RunPartialTransferOutput(const std::filesystem::path& constellationConfig,
                         const std::filesystem::path& topologyDirectory,
                         const std::filesystem::path& fixtureRoot,
                         const std::filesystem::path& outputRoot)
{
    {
        const std::filesystem::path outputDirectory = outputRoot / "partial";
        ResolvedSatComputeConfig config =
            MakeDiamondReplayTestConfig(topologyDirectory);
        config.runName = "transfer-replay-fixture";
        config.constellation = LoadConstellationDefinition(constellationConfig);
        config.logging.diagnosticMode = "failure";
        config.workloads.transferTrace = fixtureRoot / "traffic/transfers/engine-basic.json";
        config.outputDirectory = outputDirectory;
        const std::filesystem::path effectiveConfig = WriteEffectiveConfig(config);
        ReplayTopologyController controller(config);
        controller.Initialize();
        EcmpRouteRecorder routeRecorder(controller);
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
        const Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();
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
                                          std::nullopt,
                                          flowMonitor,
                                          routeRecorder.GetEvents(),
                                          controller.GetLinkState().GetDirectedLinks(),
                                          controller.GetLinkState().GetQueueDropEvents()};
        const RunOutputResult result = WriteRunOutputs(config, context, engine, nullptr);
        Check(!result.complete && result.diagnosticsGenerated,
              "partial transfer output status differs");
        Check(CountLines(outputDirectory / "ecmp-route-events.csv") == 1 &&
                  !std::filesystem::exists(outputDirectory / "size-aware-summary.json") &&
                  !std::filesystem::exists(outputDirectory / "capacity-aware-summary.json"),
              "global-first routing metric ownership differs");
        const std::filesystem::path failure = outputDirectory / "diagnostics/failure";
        Check(FirstLine(failure / "flow-drop-reasons.csv") ==
                      "flow_monitor_id,transfer_id,source_address,destination_address,protocol,"
                      "source_port,destination_port,reason_code,reason_name,dropped_packets,"
                      "dropped_bytes,flow_lost_packets,flow_reported_drop_packets,"
                      "flow_unattributed_lost_packets" &&
                  !std::filesystem::exists(failure / "incomplete-transfers.csv") &&
                  !std::filesystem::exists(failure / "incomplete-tasks.csv") &&
                  !std::filesystem::exists(failure / "diagnostic-summary.json") &&
                  !std::filesystem::exists(outputDirectory / "udp-socket-drops.csv"),
              "direct-transfer diagnostic ownership differs");
        const Json summary = ReadJson(result.runSummaryPath);
        Check(summary.at("run_status") == "PARTIAL" &&
                  summary.at("task_completion_policy") == "strict" &&
                  summary.at("diagnostics_generated") == true &&
                  summary.at("transfer").at("transfer_count") == 2 &&
                  summary.at("transfer").at("completed_transfer_count") == 0,
              "partial direct-transfer run summary differs");
    }
    ResetSimulationGlobals();
}

void
RunPartialTaskDiagnostics(const std::filesystem::path& constellationConfig,
                          const std::filesystem::path& topologyDirectory,
                          const std::filesystem::path& fixtureRoot,
                          const std::filesystem::path& outputRoot)
{
    {
        const std::filesystem::path outputDirectory = outputRoot / "partial-task";
        ResolvedSatComputeConfig config = MakeDiamondReplayTestConfig(topologyDirectory);
        config.runName = "task-failure-diagnostic-fixture";
        config.constellation = LoadConstellationDefinition(constellationConfig);
        config.logging.diagnosticMode = "failure";
        config.network.islQueueBytes = 1;
        config.workloads.computeProfile = fixtureRoot / "compute-profile-single.json";
        config.workloads.taskTrace = fixtureRoot / "task-single.json";
        config.outputDirectory = outputDirectory;
        const std::filesystem::path effectiveConfig = WriteEffectiveConfig(config);
        ReplayTopologyController controller(config);
        controller.Initialize();
        EcmpRouteRecorder routeRecorder(controller);
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
        const Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();
        Simulator::Stop(NanoSeconds(101000000));
        Simulator::Run();
        Check(!coordinator->IsComplete(), "failure diagnostic task unexpectedly completed");

        const RunOutputContext context = {effectiveConfig,
                                          outputDirectory,
                                          1234,
                                          controller.GetAppliedSnapshotCount(),
                                          controller.GetRouteComputationCount(),
                                          controller.GetFlowRouteRegistry(),
                                          std::nullopt,
                                          flowMonitor,
                                          routeRecorder.GetEvents(),
                                          controller.GetLinkState().GetDirectedLinks(),
                                          controller.GetLinkState().GetQueueDropEvents()};
        const RunOutputResult result = WriteRunOutputs(config,
                                                       context,
                                                       coordinator->GetTransferEngine(),
                                                       coordinator);
        const std::filesystem::path failure = outputDirectory / "diagnostics/failure";
        Check(!result.complete && result.diagnosticsGenerated,
              "partial task diagnostic status differs");
        Check(FirstLine(failure / "incomplete-tasks.csv") ==
                      "task_id,state,source_node_id,compute_node_id,result_node_id,"
                      "input_transfer_id,result_transfer_id,arrival_time_ns,"
                      "last_transition_time_ns,input_transfer_complete_time_ns,"
                      "queue_enter_time_ns,compute_start_time_ns,compute_complete_time_ns,"
                      "result_transfer_complete_time_ns" &&
                  FirstLine(failure / "incomplete-transfers.csv") ==
                      "transfer_id,transfer_state,source_node_id,destination_node_id,"
                      "source_address,destination_address,source_port,destination_port,"
                      "declared_size_bytes,payload_bytes_per_packet,derived_packet_count,"
                      "sent_application_bytes,sent_packet_count,received_application_bytes,"
                      "received_packet_count,missing_application_bytes,"
                      "missing_packet_count_lower_bound,arrival_time_ns,last_send_time_ns,"
                      "completion_time_ns" &&
                  FirstLine(failure / "isl-queue-drops.csv") ==
                      "simulation_time_ns,source_node_id,destination_node_id,output_interface,"
                      "packet_size_bytes,cumulative_drop_packets,cumulative_drop_bytes" &&
                  FirstLine(failure / "isl-queue-drop-summary.csv") ==
                      "source_node_id,destination_node_id,output_interface,drop_packets,"
                      "drop_bytes,first_drop_time_ns,last_drop_time_ns" &&
                  FirstLine(failure / "udp-socket-drops.csv") ==
                      "simulation_time_ns,destination_node_id,destination_address,"
                      "destination_port,packet_size_bytes,cumulative_drop_packets,"
                      "cumulative_drop_bytes,receiver_rcv_buf_bytes" &&
                  FirstLine(failure / "udp-socket-drop-summary.csv") ==
                      "destination_node_id,destination_address,destination_port,"
                      "receiver_rcv_buf_bytes,drop_packets,drop_bytes,first_drop_time_ns,"
                      "last_drop_time_ns" &&
                  FirstLine(failure / "flow-link-concentration.csv") ==
                      "source_node_id,destination_node_id,output_interface,"
                      "unique_transfer_count,planned_application_bytes,large_transfer_count,"
                      "input_transfer_count,result_transfer_count,adjacent_to_compute_node,"
                      "drop_packets,drop_bytes" &&
                  FirstLine(failure / "flow-drop-reasons.csv") ==
                      "flow_monitor_id,transfer_id,source_address,destination_address,protocol,"
                      "source_port,destination_port,reason_code,reason_name,dropped_packets,"
                      "dropped_bytes,flow_lost_packets,flow_reported_drop_packets,"
                      "flow_unattributed_lost_packets",
              "legacy failure diagnostic CSV headers differ");
        Check(CountLines(failure / "incomplete-tasks.csv") == 2 &&
                  CountLines(failure / "incomplete-transfers.csv") == 3,
              "incomplete task diagnostic row counts differ");

        const Json diagnostic = ReadJson(failure / "diagnostic-summary.json");
        const Json summary = ReadJson(result.runSummaryPath);
        Check(diagnostic.at("run_status") == "INCOMPLETE" &&
                  diagnostic.at("task_count") == 1 &&
                  diagnostic.at("incomplete_task_count") == 1 &&
                  diagnostic.at("transfer_count") == 2 &&
                  diagnostic.at("incomplete_transfer_count") == 2 &&
                  diagnostic.at("compute_node_count") == 1 &&
                  diagnostic.at("receiver_rcv_buf_bytes") ==
                      config.network.receiverRcvBufBytes &&
                  summary.at("run_status") == "PARTIAL" &&
                  summary.at("diagnostics_generated") == true,
              "partial task diagnostic summaries differ");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string constellationConfig;
    std::string topologyDirectory;
    std::string fixtureRoot;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("constellationConfig", "Four-satellite constellation", constellationConfig);
    command.AddValue("topologyDir", "Dynamic diamond topology slices", topologyDirectory);
    command.AddValue("fixtureRoot", "Independent test input root", fixtureRoot);
    command.AddValue("outputDir", "Temporary output root", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!constellationConfig.empty() && !topologyDirectory.empty() &&
                  !fixtureRoot.empty() && !outputDirectory.empty(),
              "constellation, topology, input, and output paths are required");
        RunCompleteTaskOutput(constellationConfig,
                              topologyDirectory,
                              std::filesystem::path(fixtureRoot) / "task",
                              outputDirectory);
        RunPartialTransferOutput(constellationConfig,
                                 topologyDirectory,
                                 fixtureRoot,
                                 outputDirectory);
        RunPartialTaskDiagnostics(constellationConfig,
                                  topologyDirectory,
                                  std::filesystem::path(fixtureRoot) / "task",
                                  outputDirectory);
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
