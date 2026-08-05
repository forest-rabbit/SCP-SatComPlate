/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/para.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{

void
Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void
RequireConfigError(Function function, const char* message)
{
    try
    {
        function();
    }
    catch (const ns3::SatComputeConfigError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int
main(int argc, char* argv[])
{
    try
    {
        ns3::SatComputeConfig parsedConfig = ns3::GetDefaultSatComputeConfig();
        std::string verifyOverrides;
        ns3::CommandLine command(__FILE__);
        ns3::AddSatComputeCommandLineOptions(command, parsedConfig);
        command.AddValue("verifyOverrides", "Test-only override profile", verifyOverrides);
        command.Parse(argc, argv);

        if (verifyOverrides == "transfer")
        {
            Require(parsedConfig.runName == "cli-run", "runName override failed");
            Require(parsedConfig.simulationStartSeconds == 5.0, "start override failed");
            Require(parsedConfig.simulationDurationSeconds == 15.0, "duration override failed");
            Require(parsedConfig.constellationConfig == "constellation.csv",
                    "constellation override failed");
            Require(parsedConfig.topologySource == "replay" &&
                        parsedConfig.topologyDirectory == "slices",
                    "replay overrides failed");
            Require(parsedConfig.islCandidateStrategy == "plus-grid" &&
                        parsedConfig.seamEnabled,
                    "candidate overrides failed");
            Require(parsedConfig.maxIslDistanceMeters == 7000000.0,
                    "distance gate override failed");
            Require(parsedConfig.delayMode == "distance" && parsedConfig.fixedDelaySeconds == 0.0,
                    "delay overrides failed");
            Require(parsedConfig.networkUpdateIntervalSeconds == 2.0,
                    "network interval override failed");
            Require(parsedConfig.islBandwidthBps == 1000 && parsedConfig.islMtuBytes == 65000 &&
                        parsedConfig.islQueueBytes == 2000 &&
                        parsedConfig.receiverRcvBufBytes == 3000,
                    "link overrides failed");
            Require(parsedConfig.routingMode == "global-hrw-per-flow" &&
                        parsedConfig.routingRecomputePolicy == "on-topology-change" &&
                        parsedConfig.ecmpHashSeed == 8,
                    "routing overrides failed");
            Require(parsedConfig.transferTrace == "transfers.json" &&
                        parsedConfig.computeProfile.empty() && parsedConfig.taskTrace.empty(),
                    "transfer input override failed");
            Require(parsedConfig.transferChunkMode == "size-aware" &&
                        parsedConfig.transferPayloadBytes == 2048 &&
                        parsedConfig.taskCompletionPolicy == "report",
                    "workload policy overrides failed");
            Require(!parsedConfig.topologyOnly &&
                        parsedConfig.topologySliceIntervalSeconds == 2.0 &&
                        !parsedConfig.includeFinalTopologyState,
                    "topology-only overrides failed");
            Require(parsedConfig.outputDirectory == "/tmp/cli-output" &&
                        parsedConfig.transferLogMode == "verbose" &&
                        parsedConfig.taskLogMode == "silent" &&
                        parsedConfig.diagnosticMode == "failure",
                    "output overrides failed");
            Require(parsedConfig.randomSeed == 9 && parsedConfig.randomRun == 10 &&
                        parsedConfig.randomStreamStart == 11,
                    "random overrides failed");
            ns3::ValidateSatComputeConfig(parsedConfig);
            return EXIT_SUCCESS;
        }
        if (verifyOverrides == "task")
        {
            Require(parsedConfig.computeProfile == "compute.json" &&
                        parsedConfig.taskTrace == "tasks.json" &&
                        parsedConfig.transferTrace.empty(),
                    "task input overrides failed");
            ns3::ValidateSatComputeConfig(parsedConfig);
            return EXIT_SUCCESS;
        }
        Require(verifyOverrides.empty(), "unknown override verification profile");

        const ns3::SatComputeConfig config = parsedConfig;

        Require(config.runName == "synthetic-66-fixed", "unexpected run name");
        Require(config.simulationStartSeconds == 0.0, "unexpected start time");
        Require(config.simulationDurationSeconds == 1000.0, "unexpected duration");
        Require(config.constellationConfig ==
                    "contrib/satcompute/input/topology/constellations/synthetic-66.csv",
                "unexpected constellation path");
        Require(config.topologySource == "online", "unexpected topology source");
        Require(config.topologyDirectory.empty(), "online replay directory must be empty");
        Require(config.islCandidateStrategy == "plus-grid", "unexpected ISL strategy");
        Require(!config.seamEnabled, "seam must be disabled by default");
        Require(config.maxIslDistanceMeters == 6174589.0, "unexpected ISL distance");
        Require(config.delayMode == "fixed", "unexpected delay mode");
        Require(config.fixedDelaySeconds == 0.008, "unexpected fixed delay");
        Require(config.networkUpdateIntervalSeconds == 20.0,
                "unexpected network interval");
        Require(config.islBandwidthBps == 2000000000ULL, "unexpected ISL bandwidth");
        Require(config.islMtuBytes == 1500, "unexpected ISL MTU");
        Require(config.islQueueBytes == 1500000, "unexpected ISL queue");
        Require(config.receiverRcvBufBytes == 131072, "unexpected receive buffer");
        Require(config.routingMode == "global-capacity-aware-hrw", "unexpected route mode");
        Require(config.routingRecomputePolicy == "on-topology-change",
                "unexpected recompute policy");
        Require(config.ecmpHashSeed == 1, "unexpected ECMP seed");
        Require(config.transferTrace.empty() && config.computeProfile.empty() &&
                    config.taskTrace.empty(),
                "workload inputs must be opt-in");
        Require(config.transferChunkMode == "fixed", "unexpected chunk mode");
        Require(config.transferPayloadBytes == 1024, "unexpected transfer payload");
        Require(config.taskCompletionPolicy == "strict", "unexpected completion policy");
        Require(!config.topologyOnly, "topology-only must be opt-in");
        Require(config.topologySliceIntervalSeconds == 1.0,
                "unexpected topology slice interval");
        Require(config.includeFinalTopologyState, "final topology state must be exported");
        Require(config.outputDirectory == "/tmp/satcompute-output", "unexpected output path");
        Require(config.transferLogMode == "summary", "unexpected transfer log mode");
        Require(config.taskLogMode == "summary", "unexpected task log mode");
        Require(config.diagnosticMode == "off", "unexpected diagnostic mode");
        Require(config.randomSeed == 1 && config.randomRun == 1 &&
                    config.randomStreamStart == 0,
                "unexpected random defaults");

        ns3::ValidateSatComputeConfig(config);

        ns3::SatComputeConfig distanceConfig = config;
        distanceConfig.delayMode = "distance";
        distanceConfig.fixedDelaySeconds = 0.0;
        distanceConfig.networkUpdateIntervalSeconds = 1.0;
        ns3::ValidateSatComputeConfig(distanceConfig);

        ns3::SatComputeConfig replayConfig = config;
        replayConfig.topologySource = "replay";
        replayConfig.topologyDirectory = "input/topology/examples/test";
        ns3::ValidateSatComputeConfig(replayConfig);

        ns3::SatComputeConfig taskConfig = config;
        taskConfig.computeProfile = "compute.json";
        taskConfig.taskTrace = "tasks.json";
        ns3::ValidateSatComputeConfig(taskConfig);

        Require(ns3::SatComputeSecondsToNanoseconds(0.0, "zero") == 0,
                "zero seconds conversion failed");
        Require(ns3::SatComputeSecondsToNanoseconds(0.008, "fixedDelay") == 8000000,
                "fractional seconds conversion failed");
        Require(ns3::SatComputeSecondsToNanoseconds(20.0, "networkUpdateInterval") ==
                    20000000000LL,
                "whole seconds conversion failed");
        Require(ns3::SatComputeDecimalSecondsToNanoseconds("20", "time") == 20000000000LL,
                "decimal whole seconds conversion failed");
        Require(ns3::SatComputeDecimalSecondsToNanoseconds("1.000000001", "time") ==
                    1000000001,
                "decimal nanosecond conversion failed");
        Require(ns3::SatComputeDecimalSecondsToNanoseconds("2e-9", "time") == 2,
                "decimal exponent conversion failed");
        RequireConfigError(
            []() {
                ns3::SatComputeSecondsToNanoseconds(-1.0, "negative");
            },
            "negative seconds must fail");
        RequireConfigError(
            []() {
                ns3::SatComputeSecondsToNanoseconds(
                    std::numeric_limits<double>::infinity(),
                    "infinity");
            },
            "infinite seconds must fail");
        RequireConfigError(
            []() {
                ns3::SatComputeSecondsToNanoseconds(std::numeric_limits<double>::max(),
                                                    "overflow");
            },
            "overflowing seconds must fail");
        RequireConfigError(
            []() {
                ns3::SatComputeDecimalSecondsToNanoseconds("0.0000000001", "time");
            },
            "sub-nanosecond decimal seconds must fail");
        RequireConfigError(
            []() {
                ns3::SatComputeDecimalSecondsToNanoseconds("0", "time", true);
            },
            "zero positive decimal seconds must fail");
        RequireConfigError(
            []() {
                ns3::SatComputeDecimalSecondsToNanoseconds("9223372036.854775808", "time");
            },
            "overflowing decimal seconds must fail");

        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.simulationDurationSeconds = 0.0;
                ns3::ValidateSatComputeConfig(invalid);
            },
            "zero duration must fail");
        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.networkUpdateIntervalSeconds = 0.0000000001;
                ns3::ValidateSatComputeConfig(invalid);
            },
            "an interval below one nanosecond must fail");
        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.topologySource = "replay";
                ns3::ValidateSatComputeConfig(invalid);
            },
            "replay without a topology directory must fail");
        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.routingMode = "unknown";
                ns3::ValidateSatComputeConfig(invalid);
            },
            "unknown routing mode must fail");
        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.computeProfile = "compute.json";
                ns3::ValidateSatComputeConfig(invalid);
            },
            "partial task inputs must fail");
        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.transferTrace = "transfers.json";
                invalid.computeProfile = "compute.json";
                invalid.taskTrace = "tasks.json";
                ns3::ValidateSatComputeConfig(invalid);
            },
            "mixed transfer and task inputs must fail");
        RequireConfigError(
            [config]() {
                auto invalid = config;
                invalid.transferChunkMode = "size-aware";
                ns3::ValidateSatComputeConfig(invalid);
            },
            "size-aware chunking with a small MTU must fail");
    }
    catch (const std::exception& error)
    {
        std::cerr << "para test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
