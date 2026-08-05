/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/circular-orbit-trace-exporter.h"
#include "ns3/command-line.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/para.h"
#include "ns3/resolved-config.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace ns3;

int
main(int argc, char* argv[])
{
    SatComputeConfig input = GetDefaultSatComputeConfig();
    input.runName = "topology-generator";
    input.topologySource = "online";
    input.topologyDirectory.clear();
    input.routingMode = "global-first";
    input.transferTrace.clear();
    input.computeProfile.clear();
    input.taskTrace.clear();
    input.topologyExportEnabled = true;
    input.outputDirectory = "/tmp/satcompute-topology-trace";

    CommandLine command(__FILE__);
    command.AddValue("runName", "Stable name recorded in the trace manifest", input.runName);
    command.AddValue("simulationStart",
                     "Orbit epoch start offset in seconds",
                     input.simulationStartSeconds);
    command.AddValue("simulationDuration",
                     "Positive trace duration in seconds",
                     input.simulationDurationSeconds);
    command.AddValue("constellationConfig",
                     "Path to the constellation-only JSON",
                     input.constellationConfig);
    command.AddValue("islCandidateStrategy",
                     "Fixed candidate ISL strategy",
                     input.islCandidateStrategy);
    command.AddValue("seamEnabled", "Enable seam candidate links", input.seamEnabled);
    command.AddValue("maxIslDistance",
                     "Maximum valid ISL distance in meters",
                     input.maxIslDistanceMeters);
    command.AddValue("delayMode", "Link delay mode: fixed or distance", input.delayMode);
    command.AddValue("fixedDelay",
                     "Fixed one-way link delay in seconds",
                     input.fixedDelaySeconds);
    command.AddValue("networkUpdateInterval",
                     "Intended live-network update interval in seconds",
                     input.networkUpdateIntervalSeconds);
    command.AddValue("islBandwidthBps",
                     "ISL data rate recorded in bit/s",
                     input.islBandwidthBps);
    command.AddValue("topologyExportInterval",
                     "Topology trace interval in seconds",
                     input.topologyExportIntervalSeconds);
    command.AddValue("includeFinalTopologyState",
                     "Export the exact trace end state",
                     input.includeFinalTopologyState);
    command.AddValue("outputDir", "Direct topology-trace output directory", input.outputDirectory);
    command.AddValue("randomSeed", "ns-3 global random seed", input.randomSeed);
    command.AddValue("randomRun", "ns-3 independent run number", input.randomRun);
    command.AddValue("randomStreamStart",
                     "First random stream reserved by SatCompute",
                     input.randomStreamStart);
    command.Parse(argc, argv);

    try
    {
        const ResolvedSatComputeConfig config = ResolveSatComputeConfig(input);
        RngSeedManager::SetSeed(config.randomness.seed);
        RngSeedManager::SetRun(config.randomness.run);
        RngSeedManager::ResetNextStreamIndex();

        TopologyTraceExportResult result;
        {
            OnlineOrbitConstellation constellation(config.constellation,
                                                   config.simulation.startTimeNs);
            CircularOrbitTraceExporter exporter(config, config.outputDirectory, constellation);
            exporter.Initialize();
            Simulator::Stop(NanoSeconds(config.simulation.durationNs));
            Simulator::Run();
            result = exporter.Finalize();
        }
        Simulator::Destroy();

        const nlohmann::json output = {{"application", "satcompute-topology-generator"},
                                       {"manifest", result.manifestPath.string()},
                                       {"run", config.runName},
                                       {"slice_count", result.slices.size()},
                                       {"status", "generated"}};
        std::cout << output.dump() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 2;
    }
}
