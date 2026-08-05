/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/circular-orbit-trace-exporter.h"
#include "ns3/command-line.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/online-topology-controller.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/snapshot-reader.h"
#include "ns3/snapshot-schedule.h"

#include "../support/config-factory.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeOnlineTestConfig;
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
ExpectSnapshotError(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const TopologySnapshotError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

void
ResetSimulationGlobals()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

void
WriteText(const std::filesystem::path& path, const std::string& content, bool append = false)
{
    std::ofstream output(path,
                         std::ios::binary |
                             (append ? std::ios::app : std::ios::trunc));
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write equivalence test file " + path.string());
    }
    output << content;
}

TopologyTraceExportResult
ExportTrace(const ResolvedSatComputeConfig& config,
            const std::filesystem::path& outputDirectory)
{
    TopologyTraceExportResult result;
    RngSeedManager::SetSeed(config.randomness.seed);
    RngSeedManager::SetRun(config.randomness.run);
    RngSeedManager::ResetNextStreamIndex();
    {
        OnlineOrbitConstellation constellation(config.constellation,
                                               config.simulation.startTimeNs);
        CircularOrbitTraceExporter exporter(config, outputDirectory, constellation);
        exporter.Initialize();
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        result = exporter.Finalize();
    }
    ResetSimulationGlobals();
    return result;
}

std::vector<SatelliteSnapshot>
ReadSharedSnapshots(const ResolvedSatComputeConfig& config,
                    const std::filesystem::path& traceDirectory,
                    uint32_t expectedDiscoveredCount)
{
    const SnapshotSchedule schedule = ScanSatelliteSnapshots(
        traceDirectory,
        config.simulation.durationNs,
        config.network.networkUpdateIntervalNs);
    Check(schedule.manifestAuthoritative,
          "version 0.3 trace manifest was not authoritative");
    Check(schedule.discoveredSnapshotCount == expectedDiscoveredCount,
          "fine trace discovery count differs");
    Check(schedule.selectedSnapshotCount == 3 && schedule.updates.size() == 2,
          "20-second down-sampling did not select 0, 20, and 40 seconds");

    std::vector<SatelliteSnapshot> snapshots;
    snapshots.push_back(ReadSatelliteSnapshot(schedule.initialNodesFilename,
                                              schedule.initialLinksFilename,
                                              0));
    for (const SnapshotUpdate& update : schedule.updates)
    {
        snapshots.push_back(ReadSatelliteSnapshot(update.nodesFilename,
                                                  update.linksFilename,
                                                  update.timeNs));
    }
    return snapshots;
}

std::vector<CircularOrbitTopologyState>
RunOnlineAtSharedTimes(const ResolvedSatComputeConfig& config)
{
    std::vector<CircularOrbitTopologyState> states;
    RngSeedManager::SetSeed(config.randomness.seed);
    RngSeedManager::SetRun(config.randomness.run);
    RngSeedManager::ResetNextStreamIndex();
    {
        OnlineTopologyController controller(config);
        controller.Initialize();
        states.push_back(controller.GetLastTopologyState());
        for (const int64_t timeNs : {20000000000LL, 40000000000LL})
        {
            Simulator::Schedule(NanoSeconds(timeNs), [&controller, &states] {
                states.push_back(controller.GetLastTopologyState());
            });
        }
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        Check(controller.GetAppliedUpdateTimesNs() ==
                  std::vector<int64_t>({0, 20000000000LL, 40000000000LL}),
              "online 20-second update times differ");
        Check(controller.GetRouteComputationCount() == 1,
              "delay-only shared-time updates rebuilt routes");
    }
    ResetSimulationGlobals();
    return states;
}

void
CheckSnapshotMatchesOnline(const SatelliteSnapshot& snapshot,
                           const CircularOrbitTopologyState& state,
                           uint64_t bandwidthBps)
{
    Check(snapshot.schema == SatelliteSnapshotSchema::VERSION_0_2,
          "generated snapshot did not use schema 0.2");
    Check(snapshot.simulationTimeNs == state.simulationTimeNs,
          "generated and online snapshot times differ");
    Check(snapshot.positions.size() == state.positions.size(),
          "generated and online position counts differ");
    Check(snapshot.satelliteIds.size() == state.positions.size(),
          "generated stable ID count differs");
    constexpr double coordinateToleranceM = 1e-6;
    for (std::size_t index = 0; index < snapshot.positions.size(); ++index)
    {
        const SatelliteSnapshotPosition& exported = snapshot.positions[index];
        const SatelliteEcefPosition& online = state.positions[index];
        Check(exported.satelliteId == online.satelliteId &&
                  snapshot.satelliteIds[index] == online.satelliteId,
              "generated and online stable IDs differ");
        Check(std::abs(exported.xM - online.positionM.x) <= coordinateToleranceM &&
                  std::abs(exported.yM - online.positionM.y) <= coordinateToleranceM &&
                  std::abs(exported.zM - online.positionM.z) <= coordinateToleranceM,
              "generated and online ECEF positions differ beyond tolerance");
    }
    Check(snapshot.links == state.GetActiveLinks(bandwidthBps),
          "generated and online active links or delays differ");
}

void
CheckSharedTraceEquivalence(const std::vector<SatelliteSnapshot>& oneSecond,
                            const std::vector<SatelliteSnapshot>& twoSecond,
                            const std::vector<CircularOrbitTopologyState>& online,
                            uint64_t bandwidthBps)
{
    Check(oneSecond.size() == 3 && twoSecond.size() == 3 && online.size() == 3,
          "shared-time state count differs");
    for (std::size_t index = 0; index < online.size(); ++index)
    {
        CheckSnapshotMatchesOnline(oneSecond[index], online[index], bandwidthBps);
        CheckSnapshotMatchesOnline(twoSecond[index], online[index], bandwidthBps);
        Check(oneSecond[index].simulationTimeNs == twoSecond[index].simulationTimeNs,
              "one- and two-second shared timestamps differ");
        Check(oneSecond[index].positions == twoSecond[index].positions,
              "one- and two-second ECEF states differ at a shared timestamp");
        Check(oneSecond[index].links == twoSecond[index].links,
              "one- and two-second topology states differ at a shared timestamp");
    }
}

void
RunGeneratedReplay(const ResolvedSatComputeConfig& onlineConfig,
                   const std::filesystem::path& traceDirectory)
{
    ResolvedSatComputeConfig replayConfig =
        MakeReplayTestConfig(onlineConfig, traceDirectory);
    RngSeedManager::SetSeed(replayConfig.randomness.seed);
    RngSeedManager::SetRun(replayConfig.randomness.run);
    RngSeedManager::ResetNextStreamIndex();
    {
        ReplayTopologyController controller(replayConfig);
        controller.Initialize();
        Simulator::Stop(NanoSeconds(replayConfig.simulation.durationNs));
        Simulator::Run();
        Check(controller.GetAppliedSnapshotCount() == 3,
              "generated 0.2 replay did not apply 0, 20, and 40 seconds");
        Check(controller.GetRouteComputationCount() == 1,
              "generated delay-only replay rebuilt routes");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string constellationConfig;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("constellationConfig",
                     "Four-satellite constellation JSON",
                     constellationConfig);
    command.AddValue("outputDir", "Temporary trace directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!constellationConfig.empty(), "constellationConfig is required");
        Check(!outputDirectory.empty(), "outputDir is required");
        ResolvedSatComputeConfig oneSecondConfig = MakeOnlineTestConfig(
            2,
            2,
            "distance",
            41000000000LL,
            20000000000LL,
            30000000.0L);
        oneSecondConfig.runName = "online-equivalence-1s";
        oneSecondConfig.constellation = LoadConstellationDefinition(constellationConfig);
        oneSecondConfig.traceExport = {true, 1000000000LL, true, "json-slices"};
        ResolvedSatComputeConfig twoSecondConfig = oneSecondConfig;
        twoSecondConfig.runName = "online-equivalence-2s";
        twoSecondConfig.traceExport.intervalNs = 2000000000LL;
        Check(oneSecondConfig.traceExport.intervalNs == 1000000000LL &&
                  twoSecondConfig.traceExport.intervalNs == 2000000000LL,
              "equivalence trace inputs do not use one and two seconds");
        Check(oneSecondConfig.network.networkUpdateIntervalNs == 20000000000LL &&
                  twoSecondConfig.network.networkUpdateIntervalNs == 20000000000LL,
              "equivalence network inputs do not use 20 seconds");

        const std::filesystem::path root(outputDirectory);
        const TopologyTraceExportResult oneSecondResult =
            ExportTrace(oneSecondConfig, root / "one-second");
        const TopologyTraceExportResult twoSecondResult =
            ExportTrace(twoSecondConfig, root / "two-second");
        Check(oneSecondResult.slices.size() == 42,
              "one-second trace plus final state count differs");
        Check(twoSecondResult.slices.size() == 22,
              "two-second trace plus final state count differs");

        WriteText(root / "one-second/nodes_10.5s.json", "not part of manifest\n");
        WriteText(root / "one-second/topology_10.5s.json", "not part of manifest\n");
        const std::vector<SatelliteSnapshot> oneSecondSnapshots =
            ReadSharedSnapshots(oneSecondConfig, root / "one-second", 42);
        const std::vector<SatelliteSnapshot> twoSecondSnapshots =
            ReadSharedSnapshots(twoSecondConfig, root / "two-second", 22);
        const std::vector<CircularOrbitTopologyState> onlineStates =
            RunOnlineAtSharedTimes(oneSecondConfig);
        CheckSharedTraceEquivalence(oneSecondSnapshots,
                                    twoSecondSnapshots,
                                    onlineStates,
                                    oneSecondConfig.network.linkBandwidthBps);

        RunGeneratedReplay(oneSecondConfig, root / "one-second");
        RunGeneratedReplay(twoSecondConfig, root / "two-second");

        ExpectSnapshotError(
            [&] {
                ReadSatelliteSnapshot(root / "one-second/nodes_0s.json",
                                      root / "one-second/topology_0s.json",
                                      1000000000LL);
            },
            "version 0.2 embedded time mismatch was accepted");
        WriteText(root / "one-second/nodes_0s.json", " ", true);
        ExpectSnapshotError(
            [&] {
                ScanSatelliteSnapshots(root / "one-second",
                                       oneSecondConfig.simulation.durationNs,
                                       oneSecondConfig.network.networkUpdateIntervalNs);
            },
            "tampered manifest-listed topology trace was accepted");

        std::cout << "SatCompute topology trace replay equivalence tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
