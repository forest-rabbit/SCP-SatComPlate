/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/para.h"
#include "ns3/resolved-config.h"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void
Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void
RequireResolutionError(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const ns3::ResolvedSatComputeConfigError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    ns3::CommandLine command(__FILE__);
    command.AddValue("outputDir", "Expected resolved output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Require(!outputDirectory.empty(), "outputDir is required");

        ns3::SatComputeConfig defaults = ns3::GetDefaultSatComputeConfig();
        defaults.outputDirectory = outputDirectory;
        const ns3::ResolvedSatComputeConfig resolved =
            ns3::ResolveSatComputeConfig(defaults);
        Require(resolved.schemaVersion == "0.3", "resolved schema version differs");
        Require(resolved.runName == "synthetic-66-fixed", "resolved run name differs");
        Require(resolved.simulation.startTimeNs == 0, "resolved start time differs");
        Require(resolved.simulation.durationNs == 1000000000000LL,
                "resolved duration differs");
        Require(resolved.constellation.GetSatelliteCount() == 66,
                "resolved satellite count differs");
        Require(resolved.constellation.sourcePath.is_absolute(),
                "constellation path is not absolute");
        Require(resolved.network.topologySource == "online" &&
                    !resolved.network.replayDirectory,
                "online topology resolution differs");
        Require(resolved.network.fixedDelayNs == 8000000,
                "fixed delay resolution differs");
        Require(resolved.network.networkUpdateIntervalNs == 20000000000LL,
                "network interval resolution differs");
        Require(resolved.routing.mode == "global-capacity-aware-hrw" &&
                    resolved.routing.hashSeed == 1,
                "routing resolution differs");
        Require(!resolved.workloads.transferTrace && !resolved.workloads.computeProfile &&
                    !resolved.workloads.taskTrace,
                "default workloads must be empty");
        Require(resolved.traceExport.enabled && resolved.traceExport.intervalNs == 1000000000LL,
                "trace export resolution differs");
        Require(resolved.logging.transferLogMode == "summary" &&
                    resolved.logging.taskLogMode == "summary" &&
                    resolved.logging.diagnosticMode == "off",
                "logging resolution differs");
        Require(resolved.randomness.seed == 1 && resolved.randomness.run == 1 &&
                    resolved.randomness.streamStart == 0,
                "randomness resolution differs");
        Require(resolved.outputDirectory.is_absolute(), "output directory is not absolute");

        auto distance = defaults;
        distance.runName = "synthetic-66-distance";
        distance.delayMode = "distance";
        distance.fixedDelaySeconds = 0.0;
        distance.networkUpdateIntervalSeconds = 2.0;
        const auto resolvedDistance = ns3::ResolveSatComputeConfig(distance);
        Require(!resolvedDistance.network.fixedDelayNs,
                "distance mode retained a fixed delay");
        Require(resolvedDistance.network.networkUpdateIntervalNs == 2000000000LL,
                "distance update interval differs");

        auto replay = defaults;
        replay.topologySource = "replay";
        replay.topologyDirectory =
            "contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic";
        const auto resolvedReplay = ns3::ResolveSatComputeConfig(replay);
        Require(resolvedReplay.network.replayDirectory.has_value() &&
                    resolvedReplay.network.replayDirectory->is_absolute(),
                "replay directory was not resolved");

        auto task = defaults;
        task.computeProfile =
            "contrib/satcompute/tests/fixtures/task/compute-profile-single.json";
        task.taskTrace =
            "contrib/satcompute/tests/fixtures/task/task-single.json";
        const auto resolvedTask = ns3::ResolveSatComputeConfig(task);
        Require(resolvedTask.workloads.computeProfile->is_absolute() &&
                    resolvedTask.workloads.taskTrace->is_absolute(),
                "task inputs were not resolved");

        auto missingConstellation = defaults;
        missingConstellation.constellationConfig = "/tmp/no-such-constellation.json";
        RequireResolutionError(
            [&missingConstellation]() {
                ns3::ResolveSatComputeConfig(missingConstellation);
            },
            "missing constellation was accepted");

        auto missingTransfer = defaults;
        missingTransfer.transferTrace = "/tmp/no-such-transfer.json";
        RequireResolutionError(
            [&missingTransfer]() {
                ns3::ResolveSatComputeConfig(missingTransfer);
            },
            "missing transfer trace was accepted");
    }
    catch (const std::exception& error)
    {
        std::cerr << "resolved config test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
