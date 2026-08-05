/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/circular-orbit-topology-policy.h"
#include "ns3/command-line.h"
#include "ns3/ipv4.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/simulator.h"
#include "ns3/topology-slice-exporter.h"

#include "../support/config-factory.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeOnlineTestConfig;
using satcompute::test::OnlineTestConfiguration;

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
        throw std::runtime_error("cannot open topology slice " + path.string());
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

TopologySliceExportResult
RunExport(const OnlineTestConfiguration& config,
          const std::filesystem::path& outputDirectory)
{
    const SatComputeConfig& parameters = config.parameters;
    TopologySliceExportResult result;
    {
        OnlineOrbitConstellation constellation(config.constellation);
        for (uint32_t index = 0; index < constellation.GetNodes().GetN(); ++index)
        {
            Check(constellation.GetNodes().Get(index)->GetNDevices() == 0,
                  "topology-only constellation unexpectedly has a NetDevice");
            Check(constellation.GetNodes().Get(index)->GetObject<Ipv4>() == nullptr,
                  "topology-only constellation unexpectedly has an InternetStack");
        }
        CircularOrbitTopologyPolicy policy(config.constellation,
                                           parameters.seamEnabled,
                                           parameters.maxIslDistanceMeters,
                                           parameters.delayMode,
                                           std::nullopt);
        TopologySliceExporter exporter(config.durationNs,
                                       1000000000LL,
                                       parameters.includeFinalTopologyState,
                                       parameters.islBandwidthBps,
                                       outputDirectory,
                                       constellation,
                                       policy);
        Check(exporter.GetScheduledTimesNs() ==
                  std::vector<int64_t>({0,
                                        1000000000LL,
                                        2000000000LL,
                                        2500000000LL}),
              "topology-only schedule differs");
        exporter.Initialize();
        Simulator::Stop(NanoSeconds(config.durationNs));
        Simulator::Run();
        result = exporter.Finalize();
    }
    Simulator::Destroy();
    return result;
}

void
CheckScheduleAndFilenameContracts()
{
    Check(BuildTopologySliceTimes(2500000000LL, 1000000000LL, true) ==
              std::vector<int64_t>({0,
                                    1000000000LL,
                                    2000000000LL,
                                    2500000000LL}),
          "topology final-state schedule differs");
    Check(BuildTopologySliceTimes(2500000000LL, 1000000000LL, false) ==
              std::vector<int64_t>({0, 1000000000LL, 2000000000LL}),
          "topology schedule included a disabled final state");
    Check(BuildTopologySliceTimes(2000000000LL, 1000000000LL, true) ==
              std::vector<int64_t>({0, 1000000000LL, 2000000000LL}),
          "topology schedule duplicated an interval-aligned final state");
    Check(FormatTopologySliceTimeToken(0) == "0" &&
              FormatTopologySliceTimeToken(1000000001LL) == "1.000000001" &&
              FormatTopologySliceTimeToken(2500000000LL) == "2.5",
          "topology filename token differs");
}

void
CheckExportContent(const OnlineTestConfiguration& config,
                   const std::filesystem::path& firstDirectory,
                   const std::filesystem::path& secondDirectory)
{
    const TopologySliceExportResult first = RunExport(config, firstDirectory);
    const TopologySliceExportResult second = RunExport(config, secondDirectory);
    Check(first.slices.size() == 4 && second.slices.size() == 4,
          "topology slice count differs");
    for (const TopologySliceRecord& record : first.slices)
    {
        Check(record.activeLinkCount == 0,
              "one-meter gate unexpectedly activated a candidate link");
    }

    const nlohmann::json nodesZero = ReadJson(firstDirectory / "nodes_0s.json");
    const nlohmann::json nodesOne = ReadJson(firstDirectory / "nodes_1s.json");
    Check(nodesZero.size() == 2 && nodesZero.contains("simulation_time_ns") &&
              nodesZero.contains("nodes"),
          "node slice root contains metadata outside the minimal contract");
    Check(nodesZero.at("nodes").size() == 4, "topology node count differs");
    const nlohmann::json& firstNode = nodesZero.at("nodes").at(0);
    Check(firstNode.size() == 5 && firstNode.at("node_id") == 0 &&
              firstNode.at("node_type") == "sat" && firstNode.contains("x") &&
              firstNode.contains("y") && firstNode.contains("z"),
          "node slice fields differ");
    Check(firstNode.at("x") != nodesOne.at("nodes").at(0).at("x"),
          "native ECEF position did not evolve");

    const nlohmann::json links = ReadJson(firstDirectory / "links_2.5s.json");
    Check(links.size() == 2 && links.contains("simulation_time_ns") &&
              links.contains("links"),
          "link slice root contains metadata outside the minimal contract");
    Check(links.at("links").size() == 4,
          "inactive fixed candidates were omitted from the link slice");
    const nlohmann::json& firstLink = links.at("links").at(0);
    Check(firstLink.size() == 7 && firstLink.at("node1_id") == 0 &&
              firstLink.at("node2_id") == 1 && firstLink.at("type") == "sat" &&
              firstLink.at("active") == false && firstLink.contains("distance_m") &&
              firstLink.contains("delay_ns") &&
              firstLink.at("link_bandwidth_bps") == 100000000,
          "all-candidate link fields differ");
    Check(firstLink.at("delay_ns") ==
              DistanceToPropagationDelayNs(firstLink.at("distance_m").get<double>()),
          "distance-mode slice delay differs from the shared policy");

    const std::vector<std::string> expectedNames = {"links_0s.json",
                                                    "links_1s.json",
                                                    "links_2.5s.json",
                                                    "links_2s.json",
                                                    "nodes_0s.json",
                                                    "nodes_1s.json",
                                                    "nodes_2.5s.json",
                                                    "nodes_2s.json"};
    const std::vector<std::string> firstNames = GetFilenames(firstDirectory);
    const std::vector<std::string> secondNames = GetFilenames(secondDirectory);
    Check(firstNames == expectedNames && secondNames == expectedNames,
          "topology-only file inventory differs");
    for (const std::string& filename : firstNames)
    {
        Check(ReadFile(firstDirectory / filename) == ReadFile(secondDirectory / filename),
              "repeated topology-only output differs: " + filename);
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string constellationConfig;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("constellationConfig",
                     "Four-satellite native LEO shell CSV",
                     constellationConfig);
    command.AddValue("outputDir", "Temporary test output directory", outputDirectory);
    command.Parse(argc, argv);
    try
    {
        Check(!constellationConfig.empty(), "constellationConfig is required");
        Check(!outputDirectory.empty(), "outputDir is required");
        CheckScheduleAndFilenameContracts();
        OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                              2,
                                                              "distance",
                                                              2500000000LL,
                                                              2000000000LL,
                                                              1.0L);
        config.constellation = LoadConstellationDefinition(constellationConfig);
        config.parameters.topologyOnly = true;
        config.parameters.topologySliceIntervalSeconds = 1.0;
        config.parameters.includeFinalTopologyState = true;
        config.parameters.islBandwidthBps = 100000000;
        CheckExportContent(config,
                           std::filesystem::path(outputDirectory) / "first",
                           std::filesystem::path(outputDirectory) / "second");
        std::cout << "SatCompute topology-only slice tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
