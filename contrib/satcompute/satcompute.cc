/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/constellation-definition.h"
#include "ns3/circular-orbit-topology-policy.h"
#include "ns3/ecmp-route-recorder.h"
#include "ns3/fault-controller.h"
#include "ns3/fault-para.h"
#include "ns3/fault-model-engine.h"
#include "ns3/fault-prediction-engine.h"
#include "ns3/fault-trace.h"
#include "ns3/flow-metrics.h"
#include "ns3/link-metrics-recorder.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/para.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/metrics.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include "ns3/task-trace.h"
#include "ns3/time-conversion.h"
#include "ns3/topology-slice-exporter.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace ns3;

namespace
{

[[noreturn]] void
FailConfig(std::string_view fieldName, std::string_view message)
{
    throw std::invalid_argument(std::string(fieldName) + " " + std::string(message));
}

void
RequireNotEmpty(const std::string& value, std::string_view fieldName)
{
    if (value.empty())
    {
        FailConfig(fieldName, "must not be empty");
    }
}

void
RequireChoice(const std::string& value,
              std::string_view fieldName,
              std::initializer_list<std::string_view> choices)
{
    for (const std::string_view choice : choices)
    {
        if (value == choice)
        {
            return;
        }
    }
    FailConfig(fieldName, "has an unsupported value: " + value);
}

void
RequirePositiveSeconds(double value, std::string_view fieldName)
{
    if (SatComputeSecondsToNanoseconds(value, fieldName) <= 0)
    {
        FailConfig(fieldName, "must be greater than zero");
    }
}

std::string
ResolveOptionalInputFile(const std::string& value, std::string_view fieldName)
{
    if (value.empty())
    {
        return {};
    }
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(value, error);
    if (error || !std::filesystem::is_regular_file(resolved))
    {
        FailConfig(fieldName, "must reference an existing regular file: " + value);
    }
    return resolved.string();
}

std::string
ResolveOutputFile(const std::string& value, std::string_view fieldName)
{
    RequireNotEmpty(value, fieldName);
    const std::filesystem::path resolved =
        std::filesystem::absolute(value).lexically_normal();
    std::error_code error;
    const bool exists = std::filesystem::exists(resolved, error);
    if (error || (exists && !std::filesystem::is_regular_file(resolved)))
    {
        FailConfig(fieldName, "must be a writable file path: " + value);
    }
    return resolved.string();
}

void
LogTaskInputs(const ComputeProfile& profile,
              const TaskTrace& trace,
              const std::string& logMode)
{
    if (logMode == "silent")
    {
        return;
    }
    std::cout << "[TASK]" << std::endl
              << "  compute nodes : " << profile.nodes.size() << std::endl
              << "  tasks         : " << trace.tasks.size() << std::endl;
    if (logMode != "verbose")
    {
        return;
    }
    for (const ComputeNodeProfile& node : profile.nodes)
    {
        std::cout << "  compute node  : id=" << node.nodeId
                  << " rate=" << node.computeRateWorkUnitsPerSecond << std::endl;
    }
    for (const TaskDefinition& task : trace.tasks)
    {
        std::cout << "  task          : id=" << task.taskId
                  << " source=" << task.sourceNodeId << " compute=" << task.computeNodeId
                  << " result=" << task.resultNodeId << " input=" << task.inputBytes
                  << " work=" << task.computeWorkUnits << " output=" << task.outputBytes
                  << " arrival_ns=" << task.arrivalTimeNs << std::endl;
    }
}

void
AddCommandLineOptions(CommandLine& commandLine,
                      SatComputeConfig& config,
                      FaultParameters& faultParameters)
{
    commandLine.AddValue("simulationDuration",
                         "Simulation duration in seconds",
                         config.simulationDurationSeconds);
    commandLine.AddValue("constellationConfig",
                         "Path to the native LEO shell CSV",
                         config.constellationConfig);
    commandLine.AddValue("orbitStartOffset",
                         "Orbit epoch offset represented by simulation time zero in seconds",
                         config.orbitStartOffsetSeconds);
    commandLine.AddValue("maxIslDistance",
                         "Maximum valid ISL distance in meters",
                         config.maxIslDistanceMeters);
    commandLine.AddValue("delayMode", "Link delay mode: fixed or distance", config.delayMode);
    commandLine.AddValue("fixedDelay",
                         "Fixed one-way link delay in seconds",
                         config.fixedDelaySeconds);
    commandLine.AddValue("networkUpdateInterval",
                         "Network state update interval in seconds",
                         config.networkUpdateIntervalSeconds);
    commandLine.AddValue("islBandwidthBps", "ISL data rate in bit/s", config.islBandwidthBps);
    commandLine.AddValue("islMtuBytes", "ISL MTU in bytes", config.islMtuBytes);
    commandLine.AddValue("islQueueBytes", "ISL queue capacity in bytes", config.islQueueBytes);
    commandLine.AddValue("receiverRcvBufBytes",
                         "UDP receive buffer in bytes",
                         config.receiverRcvBufBytes);
    commandLine.AddValue("routingMode", "IPv4 routing policy", config.routingMode);
    commandLine.AddValue("ecmpHashSeed", "Per-flow ECMP and HRW hash seed", config.ecmpHashSeed);
    commandLine.AddValue("computeProfile",
                         "Satellite compute profile JSON path",
                         config.computeProfile);
    commandLine.AddValue("taskTrace", "Task trace JSON path", config.taskTrace);
    commandLine.AddValue("computeDeadlineFactor",
                         "Compute-stage deadline factor (finite, at least 1)",
                         config.computeDeadlineFactor);
    commandLine.AddValue("transferChunkMode",
                         "Transfer chunking policy",
                         config.transferChunkMode);
    commandLine.AddValue("transferPayloadBytes",
                         "Fixed UDP payload size in bytes",
                         config.transferPayloadBytes);
    commandLine.AddValue("taskCompletionPolicy",
                         "Task completion policy: strict or report",
                         config.taskCompletionPolicy);
    commandLine.AddValue("faultMode", "Fault mode: none or generate", config.faultMode);
    commandLine.AddValue("faultTrace", "Generated fault trace output path", config.faultTrace);
    commandLine.AddValue("faultProbabilityAudit",
                         "Collect probability audit records and CSV outputs",
                         config.faultProbabilityAudit);
    commandLine.AddValue("faultEnableF1",
                         "Enable F1 generation and optional probability audit",
                         faultParameters.f1.enabled);
    commandLine.AddValue("faultEnableF2",
                         "Enable F2 generation and optional probability audit",
                         faultParameters.f2.enabled);
    commandLine.AddValue("faultEnableF3",
                         "Enable the built-in F3 source in generate mode",
                         faultParameters.f3.enabled);
    commandLine.AddValue("faultF1Beta",
                        "F1 temperature-to-probability shape",
                        faultParameters.f1.temperature.growthFactor);
    commandLine.AddValue("faultF1Gamma",
                        "F1 heating shape; base-to-critical time remains fixed",
                        faultParameters.f1.temperature.heatingShapeGamma);
    commandLine.AddValue("faultF3Mode", "F3 fixed_k, poisson or controlled",
                         faultParameters.f3.mode);
    commandLine.AddValue("faultF3Node", "Controlled F3 target, scheduler-only scenario truth",
                         faultParameters.f3.controlledNodeId);
    commandLine.AddValue("faultF3Time", "Controlled F3 START in seconds",
                         faultParameters.f3.controlledStartSeconds);
    commandLine.AddValue("topologyOnly",
                         "Generate topology slices without network simulation",
                         config.topologyOnly);
    commandLine.AddValue("topologySliceInterval",
                         "Topology slice interval in seconds",
                         config.topologySliceIntervalSeconds);
    commandLine.AddValue("includeFinalTopologyState",
                         "Export the simulation end state",
                         config.includeFinalTopologyState);
    commandLine.AddValue("outputDir", "Structured output directory", config.outputDirectory);
    commandLine.AddValue("taskLogMode", "Task log mode", config.taskLogMode);
    commandLine.AddValue("diagnosticMode", "Failure diagnostic mode", config.diagnosticMode);
    commandLine.AddValue("linkMetrics", "Collect directed-link window metrics", config.linkMetrics);
    commandLine.AddValue("linkMetricsInterval", "Link metric window in seconds",
                         config.linkMetricsIntervalSeconds);
    commandLine.AddValue("randomSeed", "ns-3 global random seed", config.randomSeed);
    commandLine.AddValue("randomRun", "ns-3 independent run number", config.randomRun);
}

void
ValidateConfig(const SatComputeConfig& config)
{
    RequireNotEmpty(config.constellationConfig, "constellationConfig");
    RequirePositiveSeconds(config.simulationDurationSeconds, "simulationDuration");
    if (!std::isfinite(config.orbitStartOffsetSeconds) ||
        config.orbitStartOffsetSeconds < 0.0)
    {
        FailConfig("orbitStartOffset", "must be a finite non-negative number of seconds");
    }
    if (!std::isfinite(config.maxIslDistanceMeters) || config.maxIslDistanceMeters <= 0.0)
    {
        FailConfig("maxIslDistance", "must be a finite positive number of meters");
    }
    RequireChoice(config.delayMode, "delayMode", {"fixed", "distance"});
    const int64_t fixedDelayNs =
        SatComputeSecondsToNanoseconds(config.fixedDelaySeconds, "fixedDelay");
    if (config.delayMode == "fixed" && fixedDelayNs <= 0)
    {
        FailConfig("fixedDelay", "must be greater than zero in fixed mode");
    }
    RequirePositiveSeconds(config.networkUpdateIntervalSeconds, "networkUpdateInterval");
    RequirePositiveSeconds(config.linkMetricsIntervalSeconds, "linkMetricsInterval");
    if (config.topologyOnly && config.linkMetrics)
    {
        FailConfig("linkMetrics", "requires network simulation, not topologyOnly");
    }
    if (config.islBandwidthBps == 0)
    {
        FailConfig("islBandwidthBps", "must be greater than zero");
    }
    if (config.islMtuBytes < 68)
    {
        FailConfig("islMtuBytes", "must be at least 68");
    }
    if (config.islQueueBytes == 0)
    {
        FailConfig("islQueueBytes", "must be greater than zero");
    }
    if (config.receiverRcvBufBytes == 0)
    {
        FailConfig("receiverRcvBufBytes", "must be greater than zero");
    }
    RequireChoice(config.routingMode,
                  "routingMode",
                  {"global-first",
                   "global-hash-per-flow",
                   "global-hrw-per-flow",
                   "global-size-aware-hrw",
                   "global-capacity-aware-hrw"});
    const bool hasComputeProfile = !config.computeProfile.empty();
    if (!std::isfinite(config.computeDeadlineFactor) || config.computeDeadlineFactor < 1.0)
    {
        FailConfig("computeDeadlineFactor", "must be finite and at least 1");
    }
    const bool hasTaskTrace = !config.taskTrace.empty();
    if (hasComputeProfile != hasTaskTrace)
    {
        FailConfig("workloads", "computeProfile and taskTrace must be provided together");
    }
    RequireChoice(config.transferChunkMode, "transferChunkMode", {"fixed", "size-aware"});
    if (config.transferPayloadBytes == 0 || config.transferPayloadBytes > 65507)
    {
        FailConfig("transferPayloadBytes", "must be in the range 1..65507");
    }
    if (config.transferChunkMode == "fixed" &&
        config.transferPayloadBytes + 28 > config.islMtuBytes)
    {
        FailConfig("transferPayloadBytes", "plus UDP/IPv4 headers exceeds islMtuBytes");
    }
    if (config.transferChunkMode == "size-aware" && config.islMtuBytes < 64028)
    {
        FailConfig("islMtuBytes", "must be at least 64028 for size-aware chunking");
    }
    RequireChoice(config.taskCompletionPolicy, "taskCompletionPolicy", {"strict", "report"});
    RequireChoice(config.faultMode, "faultMode", {"none", "generate"});
    if (config.faultMode == "none" && !config.faultTrace.empty())
    {
        FailConfig("faultMode", "none cannot use faultTrace");
    }
    if (config.faultMode == "generate" && config.faultTrace.empty())
    {
        FailConfig("faultMode", "generate requires faultTrace");
    }
    if (config.faultProbabilityAudit)
    {
        if (config.faultMode == "none")
        {
            FailConfig("faultProbabilityAudit", "requires faultMode=generate");
        }
        if (!hasComputeProfile)
        {
            FailConfig("faultProbabilityAudit", "requires computeProfile and taskTrace");
        }
    }
    RequirePositiveSeconds(config.topologySliceIntervalSeconds, "topologySliceInterval");
    if (config.topologyOnly && hasComputeProfile)
    {
        FailConfig("topologyOnly", "cannot load task inputs");
    }
    if (config.topologyOnly && config.faultMode != "none")
    {
        FailConfig("topologyOnly", "requires faultMode=none");
    }
    RequireNotEmpty(config.outputDirectory, "outputDir");
    RequireChoice(config.taskLogMode, "taskLogMode", {"summary", "verbose", "silent"});
    RequireChoice(config.diagnosticMode, "diagnosticMode", {"off", "failure"});
    if (config.randomSeed == 0)
    {
        FailConfig("randomSeed", "must be greater than zero");
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    SatComputeConfig inputConfig = GetDefaultSatComputeConfig();
    FaultParameters faultParameters = GetDefaultFaultParameters();
    CommandLine command(__FILE__);
    AddCommandLineOptions(command, inputConfig, faultParameters);
    command.Parse(argc, argv);

    try
    {
        ValidateConfig(inputConfig);
        if (inputConfig.faultProbabilityAudit &&
            !faultParameters.f1.enabled && !faultParameters.f2.enabled)
        {
            FailConfig("faultProbabilityAudit", "requires enabled F1 or F2");
        }
        SatComputeConfig config = inputConfig;
        config.computeProfile =
            ResolveOptionalInputFile(config.computeProfile, "computeProfile");
        config.taskTrace = ResolveOptionalInputFile(config.taskTrace, "taskTrace");
        if (config.faultMode == "generate")
        {
            config.faultTrace = ResolveOutputFile(config.faultTrace, "faultTrace");
        }
        const int64_t simulationDurationNs =
            SatComputeSecondsToNanoseconds(config.simulationDurationSeconds,
                                           "simulationDuration");
        const int64_t topologySliceIntervalNs =
            SatComputeSecondsToNanoseconds(config.topologySliceIntervalSeconds,
                                           "topologySliceInterval");
        const std::optional<int64_t> fixedDelayNs =
            config.delayMode == "fixed"
                ? std::optional<int64_t>(
                      SatComputeSecondsToNanoseconds(config.fixedDelaySeconds, "fixedDelay"))
                : std::nullopt;
        const std::filesystem::path outputDirectory =
            std::filesystem::absolute(config.outputDirectory).lexically_normal();
        const ConstellationDefinition constellationDefinition =
            LoadConstellationDefinition(config.constellationConfig);
        ValidateMaxIslDistanceAgainstOrbit(constellationDefinition.shell,
                                           config.maxIslDistanceMeters);

        RngSeedManager::SetSeed(config.randomSeed);
        RngSeedManager::SetRun(config.randomRun);
        RngSeedManager::ResetNextStreamIndex();

        if (config.topologyOnly)
        {
            TopologySliceExportResult sliceResult;
            {
                OnlineOrbitConstellation constellation(
                    constellationDefinition,
                    config.orbitStartOffsetSeconds);
                CircularOrbitTopologyPolicy policy(constellationDefinition,
                                                   constellation.GetPositions(),
                                                   config.maxIslDistanceMeters,
                                                   config.delayMode,
                                                   fixedDelayNs);
                TopologySliceExporter exporter(simulationDurationNs,
                                               topologySliceIntervalNs,
                                               config.includeFinalTopologyState,
                                               config.islBandwidthBps,
                                               outputDirectory / "topology",
                                               constellation,
                                               policy);
                exporter.Initialize();
                Simulator::Stop(NanoSeconds(simulationDurationNs));
                Simulator::Run();
                sliceResult = exporter.Finalize();
            }
            Simulator::Destroy();
            const nlohmann::json result = {
                {"application", "satcompute"},
                {"satellite_count", constellationDefinition.GetSatelliteCount()},
                {"slice_count", sliceResult.slices.size()},
                {"status", "topology-only"},
                {"topology_directory", sliceResult.outputDirectory.string()}};
            std::cout << result.dump() << std::endl;
            return 0;
        }

        int exitCode = 0;
        nlohmann::json result;
        {
            SatelliteTopology topology(config, constellationDefinition);
            topology.Initialize();
            EcmpRouteRecorder routeRecorder(topology);

            Ptr<NetworkTransferEngine> transferEngine;
            Ptr<TaskCoordinator> taskCoordinator;
            Ptr<FaultController> faultController;
            Ptr<FaultModelEngine> faultModelEngine;
            Ptr<FaultPredictionEngine> faultPredictionEngine;
            std::optional<ComputeProfile> computeProfile;
            std::optional<TaskTrace> taskTrace;
            if (!config.computeProfile.empty() && !config.taskTrace.empty())
            {
                computeProfile = ReadComputeProfile(config.computeProfile, topology);
                taskTrace = ReadTaskTrace(config.taskTrace,
                                          simulationDurationNs,
                                          topology,
                                          computeProfile.value());
                LogTaskInputs(computeProfile.value(), taskTrace.value(), config.taskLogMode);
            }
            std::vector<uint32_t> computeNodeIds;
            if (computeProfile.has_value())
            {
                computeNodeIds.reserve(computeProfile->nodes.size());
                for (const ComputeNodeProfile& node : computeProfile->nodes)
                {
                    computeNodeIds.push_back(node.nodeId);
                }
            }
            if (config.faultMode == "generate")
            {
                if (!faultParameters.f1.enabled && !faultParameters.f2.enabled &&
                    !faultParameters.f3.enabled)
                {
                    FailConfig("faultMode",
                               "generate requires at least one enabled fault source");
                }
                if ((faultParameters.f1.enabled || faultParameters.f2.enabled) &&
                    !computeProfile.has_value())
                {
                    FailConfig("faultMode",
                               "generate with enabled F1/F2 requires computeProfile and taskTrace");
                }
                faultController = CreateObject<FaultController>();
                if (config.faultProbabilityAudit && computeProfile.has_value() &&
                    (faultParameters.f1.enabled || faultParameters.f2.enabled))
                {
                    faultPredictionEngine = CreateObject<FaultPredictionEngine>();
                    faultPredictionEngine->Configure(faultParameters,
                        computeNodeIds,
                        simulationDurationNs,
                        faultController);
                    if (faultParameters.f2.enabled)
                    {
                        faultPredictionEngine->BindOrbitConstellation(
                            topology.GetOnlineConstellation());
                    }
                }
                faultController->ConfigureGeneration(
                    topology.GetIdMap().GetCanonicalSatelliteIds(),
                    simulationDurationNs);
                faultController->BindTopology(topology);
                faultModelEngine = CreateObject<FaultModelEngine>();
                faultModelEngine->Configure(faultParameters,
                                            topology.GetIdMap().GetCanonicalSatelliteIds(),
                                            computeNodeIds,
                                            simulationDurationNs,
                                            faultController,
                                            config.faultProbabilityAudit);
                if (faultParameters.f2.enabled)
                {
                    faultModelEngine->BindOrbitConstellation(
                        topology.GetOnlineConstellation());
                }
            }
            if (computeProfile.has_value() && taskTrace.has_value())
            {
                taskCoordinator = CreateObject<TaskCoordinator>();
                taskCoordinator->Initialize(computeProfile.value(),
                                            taskTrace.value(),
                                            topology,
                                            config.transferChunkMode,
                                            config.transferPayloadBytes,
                                            config.islMtuBytes,
                                            config.receiverRcvBufBytes,
                                            config.diagnosticMode == "failure",
                                            simulationDurationNs,
                                            config.computeDeadlineFactor);
                transferEngine = taskCoordinator->GetTransferEngine();
                if (faultController != nullptr)
                {
                    faultController->BindTaskCoordinator(taskCoordinator);
                }
                if (faultPredictionEngine != nullptr)
                {
                    faultPredictionEngine->BindTaskCoordinator(taskCoordinator);
                }
            }
            if (faultModelEngine != nullptr)
            {
                faultModelEngine->BindTaskCoordinator(taskCoordinator);
            }

            std::optional<LinkMetricsRecorder> linkMetrics;
            if (config.linkMetrics)
            {
                linkMetrics.emplace(topology, transferEngine, simulationDurationNs,
                    SatComputeSecondsToNanoseconds(config.linkMetricsIntervalSeconds,
                                                   "linkMetricsInterval"), outputDirectory);
            }
            else
            {
                LinkMetricsRecorder::RemoveOutputs(outputDirectory);
            }
            const Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();
            Simulator::Stop(NanoSeconds(simulationDurationNs));
            const auto wallStart = std::chrono::steady_clock::now();
            Simulator::Run();
            const auto wallStop = std::chrono::steady_clock::now();
            if (linkMetrics)
            {
                linkMetrics->Finalize();
            }
            if (config.faultMode == "generate")
            {
                const FaultTrace& generatedTrace = faultModelEngine->Finalize();
                WriteFaultTraceV2(config.faultTrace, generatedTrace);
            }
            const int64_t wallClockNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(wallStop - wallStart).count();
            std::optional<CapacityAwareRuntimeSummary> capacitySummary;
            if (config.routingMode == "global-capacity-aware-hrw")
            {
                capacitySummary = transferEngine != nullptr
                                      ? transferEngine->CollectCapacityAwareSummary()
                                      : CapacityAwareRuntimeSummary();
            }
            MetricsRuntimeContext metricsContext = {
                outputDirectory,
                simulationDurationNs,
                wallClockNs,
                topology.GetAppliedTopologySliceCount(),
                topology.GetRouteComputationCount(),
                faultController,
                faultModelEngine,
                faultPredictionEngine,
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
            if (!output.complete && config.taskCompletionPolicy == "strict")
            {
                exitCode = 3;
            }
            result = {{"application", "satcompute"},
                      {"run_summary", output.runSummaryPath.string()},
                      {"satellite_count", constellationDefinition.GetSatelliteCount()},
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
