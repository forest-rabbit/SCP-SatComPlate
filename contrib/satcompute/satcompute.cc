/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/circular-orbit-topology-policy.h"
#include "ns3/effective-config.h"
#include "ns3/ecmp-route-recorder.h"
#include "ns3/flow-metrics.h"
#include "ns3/network-transfer.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/para.h"
#include "ns3/resolved-config.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/metrics.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"
#include "ns3/topology-slice-exporter.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

using namespace ns3;

int
main(int argc, char* argv[])
{
    SatComputeConfig inputConfig = GetDefaultSatComputeConfig();
    bool validateOnly = false;
    CommandLine command(__FILE__);
    AddSatComputeCommandLineOptions(command, inputConfig);
    command.AddValue("validateOnly", "Validate and resolve inputs without simulation", validateOnly);
    command.Parse(argc, argv);

    try
    {
        if (validateOnly && inputConfig.topologyOnly)
        {
            throw std::runtime_error("validateOnly and topologyOnly are mutually exclusive");
        }

        const ResolvedSatComputeConfig config = ResolveSatComputeConfig(inputConfig);
        const std::filesystem::path effectiveConfig =
            WriteEffectiveConfig(config, validateOnly, config.topologyOnly);
        if (validateOnly)
        {
            const nlohmann::json result = {{"application", "satcompute"},
                                           {"effective_config", effectiveConfig.string()},
                                           {"satellite_count",
                                            config.constellation.GetSatelliteCount()},
                                           {"run", config.runName},
                                           {"status", "validated"}};
            std::cout << result.dump() << std::endl;
            return 0;
        }
        RngSeedManager::SetSeed(config.randomness.seed);
        RngSeedManager::SetRun(config.randomness.run);
        RngSeedManager::ResetNextStreamIndex();

        if (config.topologyOnly)
        {
            TopologySliceExportResult sliceResult;
            {
                OnlineOrbitConstellation constellation(config.constellation);
                CircularOrbitTopologyPolicy policy(config.constellation,
                                                   config.network.seamEnabled,
                                                   config.network.maxIslDistanceM,
                                                   config.network.delayMode,
                                                   config.network.fixedDelayNs);
                TopologySliceExporter exporter(config.simulation.durationNs,
                                               config.topologySlices.intervalNs,
                                               config.topologySlices.includeFinalState,
                                               config.network.linkBandwidthBps,
                                               config.outputDirectory / "topology",
                                               constellation,
                                               policy);
                exporter.Initialize();
                Simulator::Stop(NanoSeconds(config.simulation.durationNs));
                Simulator::Run();
                sliceResult = exporter.Finalize();
            }
            Simulator::Destroy();
            const nlohmann::json result = {
                {"application", "satcompute"},
                {"effective_config", effectiveConfig.string()},
                {"satellite_count", config.constellation.GetSatelliteCount()},
                {"run", config.runName},
                {"slice_count", sliceResult.slices.size()},
                {"status", "topology-only"},
                {"topology_directory", sliceResult.outputDirectory.string()}};
            std::cout << result.dump() << std::endl;
            return 0;
        }

        int exitCode = 0;
        nlohmann::json result;
        {
            SatelliteTopology topology(config);
            topology.Initialize();
            EcmpRouteRecorder routeRecorder(topology);

            Ptr<NetworkTransferEngine> transferEngine;
            Ptr<TaskCoordinator> taskCoordinator;
            if (config.workloads.transferTrace)
            {
                const NetworkTransferState networkTransfers = InstallNetworkTransfersNs(
                    *config.workloads.transferTrace,
                    config.workloads.transferChunkMode,
                    config.workloads.transferPayloadBytes,
                    config.network.islMtuBytes,
                    config.network.receiverRcvBufBytes,
                    config.logging.diagnosticMode == "failure",
                    config.logging.transferLogMode,
                    config.simulation.durationNs,
                    topology);
                transferEngine = networkTransfers.engine;
            }
            else if (config.workloads.computeProfile && config.workloads.taskTrace)
            {
                const ComputeProfile profile =
                    ReadComputeProfile(*config.workloads.computeProfile, topology);
                const TaskTrace trace = ReadTaskTrace(*config.workloads.taskTrace,
                                                      config.simulation.durationNs,
                                                      topology,
                                                      profile);
                taskCoordinator = CreateObject<TaskCoordinator>();
                taskCoordinator->Initialize(profile,
                                            trace,
                                            topology,
                                            config.workloads.transferChunkMode,
                                            config.workloads.transferPayloadBytes,
                                            config.network.islMtuBytes,
                                            config.network.receiverRcvBufBytes,
                                            config.logging.diagnosticMode == "failure",
                                            config.simulation.durationNs);
                transferEngine = taskCoordinator->GetTransferEngine();
            }

            const Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();
            Simulator::Stop(NanoSeconds(config.simulation.durationNs));
            const auto wallStart = std::chrono::steady_clock::now();
            Simulator::Run();
            const auto wallStop = std::chrono::steady_clock::now();
            const int64_t wallClockNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(wallStop - wallStart).count();
            std::optional<CapacityAwareRuntimeSummary> capacitySummary;
            if (config.routing.mode == "global-capacity-aware-hrw")
            {
                capacitySummary = transferEngine != nullptr
                                      ? transferEngine->CollectCapacityAwareSummary()
                                      : CapacityAwareRuntimeSummary();
            }
            MetricsRuntimeContext metricsContext = {
                effectiveConfig,
                config.outputDirectory,
                wallClockNs,
                topology.GetAppliedTopologySliceCount(),
                topology.GetRouteComputationCount(),
                topology.GetFlowRouteRegistry(),
                capacitySummary,
                flowMonitor,
                routeRecorder.GetEvents(),
                topology.GetIslDirectedLinks(),
                topology.GetIslQueueDropEvents()};
            MetricsRecorder metrics(config,
                                    std::move(metricsContext),
                                    transferEngine,
                                    taskCoordinator);
            const MetricsRecordResult output = metrics.Record();
            if (output.complete && taskCoordinator != nullptr)
            {
                taskCoordinator->ValidateCompleted();
            }
            if (!output.complete && config.workloads.taskCompletionPolicy == "strict")
            {
                exitCode = 3;
            }
            result = {{"application", "satcompute"},
                      {"effective_config", effectiveConfig.string()},
                      {"run_summary", output.runSummaryPath.string()},
                      {"satellite_count", config.constellation.GetSatelliteCount()},
                      {"run", config.runName},
                      {"status", output.complete ? "completed" : "partial"}};
        }
        Simulator::Destroy();
        std::cout << result.dump() << std::endl;
        return exitCode;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 2;
    }
}
