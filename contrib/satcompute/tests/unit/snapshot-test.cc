/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/scenario-config.h"
#include "ns3/snapshot-reader.h"
#include "ns3/snapshot-schedule.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

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
WriteText(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot create test fixture: " + path.string());
    }
    output << contents;
}

void
WriteMinimalNodes(const std::filesystem::path& path)
{
    WriteText(path,
              R"({"nodes":[{"node_id":0,"node_type":"sat"},)"
              R"({"node_id":1,"node_type":"sat"}]})");
}

void
WriteEmptyTopology(const std::filesystem::path& path)
{
    WriteText(path, R"({"links":[]})");
}

void
WritePair(const std::filesystem::path& directory, const std::string& timeToken)
{
    WriteMinimalNodes(directory / ("nodes_" + timeToken + "s.json"));
    WriteEmptyTopology(directory / ("topology_" + timeToken + "s.json"));
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenarioFilename;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("scenario", "JSON-replay scenario fixture", scenarioFilename);
    command.AddValue("outputDir", "Temporary test output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!scenarioFilename.empty(), "scenario is required");
        Check(!outputDirectory.empty(), "outputDir is required");
        const ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        Check(config.constellation.orbitProvider == "json-replay",
              "scenario orbit provider is not json-replay");
        Check(config.network.topologySource == "json-replay",
              "scenario topology source is not json-replay");
        Check(config.network.replayDirectory.has_value(),
              "scenario replay directory was not resolved");
        const std::filesystem::path fixture = *config.network.replayDirectory;
        const std::filesystem::path output(outputDirectory);
        std::filesystem::create_directories(output);

        const SnapshotSchedule everyTwoSeconds =
            ScanSatelliteSnapshots(fixture,
                                   config.simulation.durationNs,
                                   config.network.networkUpdateIntervalNs);
        Check(everyTwoSeconds.discoveredSnapshotCount == 3,
              "discovered snapshot count differs");
        Check(!everyTwoSeconds.manifestAuthoritative,
              "legacy snapshot directory unexpectedly required a manifest");
        Check(everyTwoSeconds.selectedSnapshotCount == 3,
              "selected snapshot count differs");
        Check(everyTwoSeconds.updates.size() == 2, "two-second update count differs");
        Check(everyTwoSeconds.updates.at(0).timeNs == 2000000000,
              "first update timestamp differs");
        Check(everyTwoSeconds.updates.at(1).timeNs == 4000000000,
              "second update timestamp differs");

        const SnapshotSchedule everyFourSeconds =
            ScanSatelliteSnapshots(fixture, 5000000000, 4000000000);
        Check(everyFourSeconds.discoveredSnapshotCount == 3,
              "fine replay slices were not discovered");
        Check(everyFourSeconds.selectedSnapshotCount == 2,
              "scenario cadence did not down-select replay slices");
        Check(everyFourSeconds.updates.size() == 1 &&
                  everyFourSeconds.updates.front().timeNs == 4000000000,
              "four-second replay cadence differs");

        const SnapshotSchedule stopBoundary =
            ScanSatelliteSnapshots(fixture, 4000000000, 2000000000);
        Check(stopBoundary.selectedSnapshotCount == 2,
              "network update was selected at the simulation stop boundary");

        ExpectSnapshotError(
            [&fixture] { ScanSatelliteSnapshots(fixture, 5000000000, 1000000000); },
            "missing configured update snapshot was accepted");
        ExpectSnapshotError(
            [&fixture] { ScanSatelliteSnapshots(fixture, 0, 1000000000); },
            "zero simulation duration was accepted");
        ExpectSnapshotError(
            [&fixture] { ScanSatelliteSnapshots(fixture, 1000000000, 0); },
            "zero update interval was accepted");

        const SatelliteSnapshot initial = ReadSatelliteSnapshot(
            fixture / "nodes_0s.json",
            fixture / "topology_0s.json");
        Check(initial.satelliteIds == std::vector<uint32_t>({0, 1, 2, 3}),
              "satellite IDs are not canonical");
        Check(initial.links.size() == 4, "initial link count differs");
        Check(initial.links.at(0) == SatelliteLink{0, 1, 1000000, 100000000},
              "legacy link units or canonical ordering differ");

        const SatelliteSnapshot update = ReadSatelliteSnapshot(
            fixture / "nodes_2s.json",
            fixture / "topology_2s.json");
        Check(update.links.size() == 3, "dynamic link count differs");
        Check(update.links.at(0).sourceId == 0 && update.links.at(0).destinationId == 1,
              "dynamic links are not canonical");

        const std::filesystem::path emptyCase = output / "empty-links";
        WritePair(emptyCase, "0");
        Check(ReadSatelliteSnapshot(emptyCase / "nodes_0s.json",
                                    emptyCase / "topology_0s.json")
                  .links.empty(),
              "valid empty active-edge set was rejected");

        const std::filesystem::path duplicateTime = output / "duplicate-time";
        WritePair(duplicateTime, "0");
        WritePair(duplicateTime, "0.0");
        ExpectSnapshotError(
            [&duplicateTime] { ScanSatelliteSnapshots(duplicateTime, 1, 1); },
            "duplicate canonical timestamp was accepted");

        const std::filesystem::path subNanosecond = output / "sub-nanosecond";
        WritePair(subNanosecond, "0");
        WritePair(subNanosecond, "0.0000000001");
        ExpectSnapshotError(
            [&subNanosecond] { ScanSatelliteSnapshots(subNanosecond, 1, 1); },
            "sub-nanosecond filename was accepted");

        const std::filesystem::path malformedName = output / "malformed-name";
        WritePair(malformedName, "0");
        WriteMinimalNodes(malformedName / "nodes_1.json");
        ExpectSnapshotError(
            [&malformedName] { ScanSatelliteSnapshots(malformedName, 1, 1); },
            "malformed topology filename was silently ignored");

        const std::filesystem::path unknownField = output / "unknown-field";
        WriteText(unknownField / "nodes_0s.json",
                  R"({"nodes":[{"node_id":0,"node_type":"sat","extra":true}]})");
        WriteEmptyTopology(unknownField / "topology_0s.json");
        ExpectSnapshotError(
            [&unknownField] {
                ReadSatelliteSnapshot(unknownField / "nodes_0s.json",
                                      unknownField / "topology_0s.json");
            },
            "unknown snapshot field was accepted");

        const std::filesystem::path unknownEndpoint = output / "unknown-endpoint";
        WriteMinimalNodes(unknownEndpoint / "nodes_0s.json");
        WriteText(unknownEndpoint / "topology_0s.json",
                  R"({"links":[{"node1_id":0,"node2_id":2,"type":"sat",)"
                  R"("delay":1,"link_bandwidth":1}]})");
        ExpectSnapshotError(
            [&unknownEndpoint] {
                ReadSatelliteSnapshot(unknownEndpoint / "nodes_0s.json",
                                      unknownEndpoint / "topology_0s.json");
            },
            "unknown link endpoint was accepted");

        std::cout << "SatCompute topology snapshot tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
