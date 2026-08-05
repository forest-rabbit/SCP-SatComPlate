/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/effective-config.h"
#include "ns3/para.h"
#include "ns3/resolved-config.h"
#include "ns3/sha256.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

std::string
ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("cannot read effective config: " + path.string());
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    ns3::CommandLine command(__FILE__);
    command.AddValue("outputDir", "Temporary effective config root", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Require(!outputDirectory.empty(), "outputDir is required");
        const std::filesystem::path outputRoot = outputDirectory;

        auto defaults = ns3::GetDefaultSatComputeConfig();
        defaults.outputDirectory = (outputRoot / "default").string();
        const auto resolved = ns3::ResolveSatComputeConfig(defaults);
        const std::filesystem::path defaultPath = ns3::WriteEffectiveConfig(resolved);
        const std::string first = ReadFile(defaultPath);
        const std::string second = ReadFile(ns3::WriteEffectiveConfig(resolved));
        Require(first == second, "effective config output is not deterministic");

        const nlohmann::json effective = nlohmann::json::parse(first);
        Require(effective.at("schema_version") == "0.3", "schema version differs");
        Require(effective.at("run_name") == "synthetic-66-fixed", "run name differs");
        Require(!effective.contains("scenario_name") && !effective.contains("source_path"),
                "legacy scenario fields remain");
        Require(effective.at("simulation").at("duration_ns") == 1000000000000LL,
                "duration differs");
        Require(effective.at("network").at("fixed_delay_ns") == 8000000,
                "fixed delay differs");
        Require(effective.at("input_manifest").at("replay_topology").is_null(),
                "online mode recorded replay inputs");
        const auto& constellationInput =
            effective.at("input_manifest").at("constellation_config");
        Require(constellationInput.at("sha256") ==
                    ns3::Sha256File(resolved.constellation.sourcePath),
                "constellation hash differs");
        Require(!effective.at("input_manifest").contains("scenario_config"),
                "legacy scenario hash remains");

        auto task = defaults;
        task.outputDirectory = (outputRoot / "task").string();
        task.computeProfile =
            "contrib/satcompute/tests/fixtures/task/compute-profile-single.json";
        task.taskTrace =
            "contrib/satcompute/tests/fixtures/task/task-single.json";
        const auto resolvedTask = ns3::ResolveSatComputeConfig(task);
        const auto taskEffective = nlohmann::json::parse(
            ReadFile(ns3::WriteEffectiveConfig(resolvedTask, true, false)));
        Require(taskEffective.at("operational").at("validate_only") == true,
                "validate-only flag differs");
        Require(taskEffective.at("input_manifest").at("compute_profile").at("sha256") ==
                    ns3::Sha256File(*resolvedTask.workloads.computeProfile),
                "compute profile hash differs");
        Require(taskEffective.at("input_manifest").at("task_trace").at("sha256") ==
                    ns3::Sha256File(*resolvedTask.workloads.taskTrace),
                "task trace hash differs");

        auto replay = defaults;
        replay.outputDirectory = (outputRoot / "replay").string();
        replay.topologySource = "replay";
        replay.topologyDirectory =
            "contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic";
        replay.simulationDurationSeconds = 5.0;
        replay.networkUpdateIntervalSeconds = 2.0;
        const auto resolvedReplay = ns3::ResolveSatComputeConfig(replay);
        const auto replayEffective = nlohmann::json::parse(
            ReadFile(ns3::WriteEffectiveConfig(resolvedReplay)));
        const auto& replayManifest =
            replayEffective.at("input_manifest").at("replay_topology");
        Require(replayManifest.at("selected_snapshots").size() == 3,
                "selected replay snapshot count differs");
        Require(replayManifest.at("selected_snapshots").at(1).at("time_ns") == 2000000000LL,
                "selected replay time differs");
        Require(replayManifest.at("selected_snapshots").at(1).at("nodes").contains("sha256") &&
                    replayManifest.at("selected_snapshots")
                        .at(1)
                        .at("topology")
                        .contains("sha256"),
                "replay pair hashes are incomplete");
    }
    catch (const std::exception& error)
    {
        std::cerr << "effective config test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
