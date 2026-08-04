/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/circular-orbit-trace-exporter.h"
#include "ns3/command-line.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"

#include "../../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

std::string
ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("cannot open topology trace output " + path.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

nlohmann::json
ReadJson(const std::filesystem::path& path)
{
    return nlohmann::json::parse(ReadFile(path));
}

TopologyTraceExportResult
RunExport(const ScenarioConfig& config, const std::filesystem::path& outputDirectory)
{
    TopologyTraceExportResult result;
    {
        OnlineOrbitConstellation constellation(config.constellation);
        CircularOrbitTraceExporter exporter(config, outputDirectory, constellation);
        Check(exporter.GetScheduledTimesNs() ==
                  std::vector<int64_t>({0,
                                        1000000000LL,
                                        2000000000LL,
                                        2500000000LL}),
              "trace schedule differs from its one-second interval and final state");
        exporter.Initialize();
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        result = exporter.Finalize();
    }
    Simulator::Destroy();
    return result;
}

void
CheckScheduleAndFilenameContracts()
{
    Check(BuildTopologyTraceTimes(2500000000LL, 1000000000LL, true) ==
              std::vector<int64_t>({0,
                                    1000000000LL,
                                    2000000000LL,
                                    2500000000LL}),
          "trace final-state schedule differs");
    Check(BuildTopologyTraceTimes(2500000000LL, 1000000000LL, false) ==
              std::vector<int64_t>({0, 1000000000LL, 2000000000LL}),
          "trace schedule included a disabled final state");
    Check(BuildTopologyTraceTimes(2000000000LL, 1000000000LL, true) ==
              std::vector<int64_t>({0, 1000000000LL, 2000000000LL}),
          "trace schedule duplicated an interval-aligned final state");
    Check(BuildTopologyTraceTimes(2000000000LL, 3000000000LL, false) ==
              std::vector<int64_t>({0}),
          "long trace interval created an interior state");
    Check(FormatTopologyTraceTimeToken(0) == "0" &&
              FormatTopologyTraceTimeToken(1000000001LL) == "1.000000001" &&
              FormatTopologyTraceTimeToken(2500000000LL) == "2.5",
          "canonical trace filename token differs");
}

std::vector<std::string>
GetFilenames(const std::filesystem::path& directory)
{
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void
CheckExportContent(const ScenarioConfig& config,
                   const std::filesystem::path& firstDirectory,
                   const std::filesystem::path& secondDirectory)
{
    const TopologyTraceExportResult first = RunExport(config, firstDirectory);
    const TopologyTraceExportResult second = RunExport(config, secondDirectory);
    Check(first.slices.size() == 4 && second.slices.size() == 4,
          "trace slice count differs");

    const nlohmann::json manifest = ReadJson(first.manifestPath);
    Check(manifest.at("schema_version") == "0.2", "trace manifest version differs");
    Check(manifest.at("state_semantics") == "orbit-policy-evaluation",
          "trace manifest state semantics differ");
    Check(manifest.at("coordinate_frame") == "ECEF", "trace coordinate frame differs");
    Check(manifest.at("speed_of_light_m_per_s") == 299792458,
          "trace propagation constant differs");
    Check(manifest.at("trace_interval_ns") == 1000000000,
          "trace interval was not recorded");
    Check(manifest.at("network_update_interval_ns") == 2000000000,
          "independent network interval was not recorded");
    Check(manifest.at("slice_count") == 4, "trace manifest slice count differs");
    Check(manifest.at("slices").at(3).at("simulation_time_ns") == 2500000000LL,
          "trace final time differs");

    const nlohmann::json nodesZero = ReadJson(firstDirectory / "nodes_0s.json");
    const nlohmann::json nodesOne = ReadJson(firstDirectory / "nodes_1s.json");
    Check(nodesZero.at("node_count") == 4, "trace node count differs");
    Check(nodesZero.at("state_semantics") == "orbit-policy-evaluation",
          "node slice state semantics differ");
    Check(nodesZero.at("nodes").at(0).at("node_id") == 0,
          "trace stable node ID differs");
    Check(nodesZero.at("nodes").at(0).contains("x_m") &&
              nodesZero.at("nodes").at(0).contains("y_m") &&
              nodesZero.at("nodes").at(0).contains("z_m"),
          "trace ECEF coordinate fields are missing");
    Check(nodesZero.at("nodes").at(0).at("x_m") !=
              nodesOne.at("nodes").at(0).at("x_m"),
          "trace ECEF position did not evolve");

    const nlohmann::json topology = ReadJson(firstDirectory / "topology_2.5s.json");
    Check(topology.at("state_semantics") == "orbit-policy-evaluation",
          "topology slice state semantics differ");
    Check(topology.at("candidate_link_count") == 4 &&
              topology.at("active_link_count") == 4,
          "trace active plus-grid count differs");
    const nlohmann::json& firstLink = topology.at("links").at(0);
    Check(firstLink.at("node1_id") == 0 && firstLink.at("node2_id") == 1,
          "trace link order is not canonical");
    Check(firstLink.contains("candidate_kind") && firstLink.contains("distance_m") &&
              firstLink.contains("delay_ns") && firstLink.contains("link_bandwidth_bps"),
          "trace link state fields are missing");

    const std::vector<std::string> firstNames = GetFilenames(firstDirectory);
    const std::vector<std::string> secondNames = GetFilenames(secondDirectory);
    Check(firstNames == secondNames, "repeated trace filenames differ");
    for (const std::string& filename : firstNames)
    {
        Check(ReadFile(firstDirectory / filename) == ReadFile(secondDirectory / filename),
              "repeated topology trace differs: " + filename);
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenarioFilename;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("scenario", "Trace-enabled online scenario", scenarioFilename);
    command.AddValue("outputDir", "Temporary test output directory", outputDirectory);
    command.Parse(argc, argv);
    try
    {
        Check(!scenarioFilename.empty(), "scenario is required");
        Check(!outputDirectory.empty(), "outputDir is required");
        CheckScheduleAndFilenameContracts();
        const ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        CheckExportContent(config,
                           std::filesystem::path(outputDirectory) / "first",
                           std::filesystem::path(outputDirectory) / "second");
        std::cout << "SatCompute topology trace exporter tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
