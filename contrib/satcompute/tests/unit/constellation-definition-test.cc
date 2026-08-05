/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/constellation-definition.h"

#include "../../third-party/nlohmann/json.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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

void
RequireDefinitionError(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const ns3::ConstellationDefinitionError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

nlohmann::json
ReadJson(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        throw std::runtime_error("cannot read test constellation: " + path.string());
    }
    return nlohmann::json::parse(input);
}

std::filesystem::path
WriteJson(const std::filesystem::path& directory,
          const std::string& filename,
          const nlohmann::json& payload)
{
    const std::filesystem::path path = directory / filename;
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write test constellation: " + path.string());
    }
    output << payload.dump(2) << '\n';
    return path;
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string validPath;
    std::string invalidRuntimePath;
    std::string outputDirectory;
    ns3::CommandLine command(__FILE__);
    command.AddValue("valid", "Valid constellation-only JSON", validPath);
    command.AddValue("invalidRuntime", "Constellation with a runtime field", invalidRuntimePath);
    command.AddValue("outputDir", "Temporary mutation directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Require(!validPath.empty(), "valid fixture is required");
        Require(!invalidRuntimePath.empty(), "invalidRuntime fixture is required");
        Require(!outputDirectory.empty(), "outputDir is required");
        std::filesystem::create_directories(outputDirectory);

        const ns3::ConstellationDefinition definition =
            ns3::LoadConstellationDefinition(validPath);
        Require(definition.schemaVersion == "0.1", "schema version differs");
        Require(definition.sourcePath.is_absolute(), "source path is not absolute");
        Require(definition.constellationName == "synthetic-66", "name differs");
        Require(definition.constellationPattern == "walker-star", "pattern differs");
        Require(definition.numOrbits == 6 && definition.satellitesPerOrbit == 11,
                "Walker dimensions differ");
        Require(definition.GetSatelliteCount() == 66, "satellite count differs");
        Require(definition.altitudeM == 780000.0L, "altitude differs");
        Require(std::abs(definition.inclinationDeg - 86.4L) < 1e-12L,
                "inclination differs");
        Require(definition.phaseDiff, "phase rule differs");
        Require(definition.orbitEpochOffsetNs == 0, "epoch offset differs");

        RequireDefinitionError(
            [&invalidRuntimePath]() {
                ns3::LoadConstellationDefinition(invalidRuntimePath);
            },
            "runtime field was accepted");

        const nlohmann::json valid = ReadJson(validPath);
        auto missing = valid;
        missing.erase("phase_diff");
        const auto missingPath = WriteJson(outputDirectory, "missing.json", missing);
        RequireDefinitionError(
            [&missingPath]() {
                ns3::LoadConstellationDefinition(missingPath);
            },
            "missing field was accepted");

        auto booleanCount = valid;
        booleanCount["num_orbits"] = true;
        const auto booleanCountPath =
            WriteJson(outputDirectory, "boolean-count.json", booleanCount);
        RequireDefinitionError(
            [&booleanCountPath]() {
                ns3::LoadConstellationDefinition(booleanCountPath);
            },
            "boolean orbit count was accepted");

        auto excessive = valid;
        excessive["num_orbits"] = 500;
        excessive["satellites_per_orbit"] = 500;
        const auto excessivePath = WriteJson(outputDirectory, "excessive.json", excessive);
        RequireDefinitionError(
            [&excessivePath]() {
                ns3::LoadConstellationDefinition(excessivePath);
            },
            "excessive satellite count was accepted");

        auto invalidInclination = valid;
        invalidInclination["inclination_deg"] = 180;
        const auto invalidInclinationPath =
            WriteJson(outputDirectory, "inclination.json", invalidInclination);
        RequireDefinitionError(
            [&invalidInclinationPath]() {
                ns3::LoadConstellationDefinition(invalidInclinationPath);
            },
            "180-degree inclination was accepted");

        auto negativeEpoch = valid;
        negativeEpoch["orbit_epoch_offset_s"] = -1;
        const auto negativeEpochPath = WriteJson(outputDirectory, "epoch.json", negativeEpoch);
        RequireDefinitionError(
            [&negativeEpochPath]() {
                ns3::LoadConstellationDefinition(negativeEpochPath);
            },
            "negative epoch offset was accepted");
    }
    catch (const std::exception& error)
    {
        std::cerr << "constellation definition test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
