/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/circular-orbit-trace-exporter.h"
#include "ns3/network-transfer-config.h"
#include "ns3/network-transfer-engine.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/online-topology-controller.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/resolved-config.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/run-output-writer.h"
#include "ns3/satcompute-version.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"

#include "third-party/nlohmann/json.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

int
main(int argc, char* argv[])
{
    std::string scenarioConfig;
    std::string outputDirectory = "/tmp/satcompute-output";
    bool validateOnly = false;
    bool exportOnly = false;
    CommandLine command(__FILE__);
    command.AddValue("scenarioConfig", "Path to authoritative scenario 0.2 JSON", scenarioConfig);
    command.AddValue("outputDir", "Operational output directory", outputDirectory);
    command.AddValue("validateOnly", "Validate and resolve inputs without simulation", validateOnly);
    command.AddValue("exportOnly", "Generate online orbit/topology JSON slices only", exportOnly);
    command.Parse(argc, argv);

    try
    {
        if (scenarioConfig.empty())
        {
            if (validateOnly || exportOnly)
            {
                throw std::runtime_error("scenarioConfig is required for validateOnly or exportOnly");
            }
            std::cout << "{\"application\":\"satcompute\",\"scenario_schema_version\":\""
                      << GetSatComputeSchemaVersion() << "\",\"status\":\"ready\"}" << std::endl;
            return 0;
        }

        if (validateOnly && exportOnly)
        {
            throw std::runtime_error("validateOnly and exportOnly are mutually exclusive");
        }

        const ScenarioConfig legacyConfig = LoadScenarioConfig(scenarioConfig);
        const ResolvedSatComputeConfig config =
            ResolveLegacyScenarioConfig(legacyConfig, outputDirectory);
        const std::filesystem::path effectiveConfig =
            WriteEffectiveConfig(legacyConfig, outputDirectory, validateOnly, exportOnly);
        if (validateOnly)
        {
            const nlohmann::json result = {{"application", "satcompute"},
                                           {"effective_config", effectiveConfig.string()},
                                           {"satellite_count",
                                            config.constellation.GetSatelliteCount()},
                                           {"scenario", config.runName},
                                           {"status", "validated"}};
            std::cout << result.dump() << std::endl;
            return 0;
        }
        if (exportOnly && (!config.traceExport.enabled ||
                           config.network.topologySource != "online"))
        {
            throw std::runtime_error(
                "exportOnly requires enabled trace export with online topology");
        }
        if (!exportOnly && config.traceExport.enabled &&
            config.network.topologySource != "online")
        {
            throw std::runtime_error("trace export during simulation requires online topology");
        }

        RngSeedManager::SetSeed(config.randomness.seed);
        RngSeedManager::SetRun(config.randomness.run);
        RngSeedManager::ResetNextStreamIndex();

        if (exportOnly)
        {
            TopologyTraceExportResult traceResult;
            {
                OnlineOrbitConstellation constellation(config.constellation,
                                                       config.simulation.startTimeNs);
                CircularOrbitTraceExporter exporter(config,
                                                     std::filesystem::path(outputDirectory) /
                                                         "topology-trace",
                                                     constellation);
                exporter.Initialize();
                Simulator::Stop(NanoSeconds(config.simulation.durationNs));
                Simulator::Run();
                traceResult = exporter.Finalize();
            }
            Simulator::Destroy();
            const nlohmann::json result = {
                {"application", "satcompute"},
                {"effective_config", effectiveConfig.string()},
                {"satellite_count", config.constellation.GetSatelliteCount()},
                {"scenario", config.runName},
                {"status", "exported"},
                {"topology_trace_manifest", traceResult.manifestPath.string()}};
            std::cout << result.dump() << std::endl;
            return 0;
        }

        int exitCode = 0;
        nlohmann::json result;
        std::optional<std::filesystem::path> topologyTraceManifest;
        {
            std::unique_ptr<SatelliteTopologyController> controller;
            if (config.network.topologySource == "replay")
            {
                controller = std::make_unique<ReplayTopologyController>(config);
            }
            else
            {
                controller = std::make_unique<OnlineTopologyController>(config);
            }
            controller->Initialize();

            std::unique_ptr<CircularOrbitTraceExporter> traceExporter;
            if (config.traceExport.enabled)
            {
                const auto onlineController =
                    dynamic_cast<OnlineTopologyController*>(controller.get());
                if (onlineController == nullptr)
                {
                    throw std::runtime_error(
                        "online trace export could not access the orbit controller");
                }
                traceExporter = std::make_unique<CircularOrbitTraceExporter>(
                    config,
                    std::filesystem::path(outputDirectory) / "topology-trace",
                    onlineController->GetConstellation());
                traceExporter->Initialize();
            }

            Ptr<NetworkTransferEngine> transferEngine;
            Ptr<TaskCoordinator> taskCoordinator;
            if (config.workloads.transferTrace)
            {
                std::vector<NetworkTransfer> plans =
                    ReadNetworkTransferTrace(*config.workloads.transferTrace,
                                             config.simulation.durationNs,
                                             config.workloads.transferChunkMode,
                                             config.workloads.transferPayloadBytes,
                                             *controller);
                transferEngine = CreateObject<NetworkTransferEngine>();
                transferEngine->Configure(*controller,
                                          config.workloads.transferChunkMode,
                                          config.workloads.transferPayloadBytes,
                                          config.network.islMtuBytes,
                                          config.network.receiverRcvBufBytes,
                                          true,
                                          config.simulation.durationNs);
                transferEngine->RegisterPlans(std::move(plans));
                transferEngine->ScheduleDeclaredTransfers();
            }
            else if (config.workloads.computeProfile && config.workloads.taskTrace)
            {
                const ComputeProfile profile =
                    ReadComputeProfile(*config.workloads.computeProfile, *controller);
                const TaskTrace trace = ReadTaskTrace(*config.workloads.taskTrace,
                                                      config.simulation.durationNs,
                                                      *controller,
                                                      profile);
                taskCoordinator = CreateObject<TaskCoordinator>();
                taskCoordinator->Initialize(profile,
                                            trace,
                                            *controller,
                                            config.workloads.transferChunkMode,
                                            config.workloads.transferPayloadBytes,
                                            config.network.islMtuBytes,
                                            config.network.receiverRcvBufBytes,
                                            true,
                                            config.simulation.durationNs);
                transferEngine = taskCoordinator->GetTransferEngine();
            }

            Simulator::Stop(NanoSeconds(config.simulation.durationNs));
            const auto wallStart = std::chrono::steady_clock::now();
            Simulator::Run();
            const auto wallStop = std::chrono::steady_clock::now();
            const int64_t wallClockNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(wallStop - wallStart).count();
            if (traceExporter)
            {
                topologyTraceManifest = traceExporter->Finalize().manifestPath;
            }

            std::optional<CapacityAwareRuntimeSummary> capacitySummary;
            if (config.routing.mode == "global-capacity-aware-hrw")
            {
                capacitySummary = transferEngine != nullptr
                                      ? transferEngine->CollectCapacityAwareSummary()
                                      : CapacityAwareRuntimeSummary();
            }
            const RunOutputContext outputContext = {
                effectiveConfig,
                outputDirectory,
                wallClockNs,
                controller->GetAppliedTopologySliceCount(),
                controller->GetRouteComputationCount(),
                controller->GetFlowRouteRegistry(),
                capacitySummary};
            const RunOutputResult output = WriteRunOutputs(config,
                                                           outputContext,
                                                           transferEngine,
                                                           taskCoordinator);
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
                      {"scenario", config.runName},
                      {"topology_trace_manifest",
                       topologyTraceManifest
                           ? nlohmann::json(topologyTraceManifest->string())
                           : nlohmann::json(nullptr)},
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
