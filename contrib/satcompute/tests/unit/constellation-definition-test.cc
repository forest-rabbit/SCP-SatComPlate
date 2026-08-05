/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/constellation-definition.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::filesystem::path
WriteCsv(const std::filesystem::path& directory,
         const std::string& filename,
         const std::string& contents)
{
    const std::filesystem::path path = directory / filename;
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write test constellation: " + path.string());
    }
    output << contents;
    return path;
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string validPath;
    std::string outputDirectory;
    ns3::CommandLine command(__FILE__);
    command.AddValue("valid", "Valid native ns-3 LEO shell CSV", validPath);
    command.AddValue("outputDir", "Temporary mutation directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Require(!validPath.empty(), "valid fixture is required");
        Require(!outputDirectory.empty(), "outputDir is required");
        std::filesystem::create_directories(outputDirectory);

        const ns3::ConstellationDefinition definition =
            ns3::LoadConstellationDefinition(validPath);
        Require(definition.sourcePath.is_absolute(), "source path is not absolute");
        Require(definition.shell.planes == 6 && definition.shell.sats == 11,
                "Walker dimensions differ");
        Require(definition.GetSatelliteCount() == 66, "satellite count differs");
        Require(std::abs(definition.shell.alt - 780.0) < 1e-12, "altitude differs");
        Require(std::abs(definition.shell.inc - 86.4) < 1e-12, "inclination differs");
        Require(std::abs(definition.shell.phasing - 1.0) < 1e-12, "phasing differs");
        Require(std::abs(definition.shell.raanSpanDeg - 180.0) < 1e-12,
                "RAAN span differs");

        const auto missing = WriteCsv(outputDirectory, "missing.csv", "# no shell row\n");
        RequireDefinitionError(
            [&missing]() {
                ns3::LoadConstellationDefinition(missing);
            },
            "missing shell row was accepted");

        const auto multiple =
            WriteCsv(outputDirectory, "multiple.csv", "780,86.4,6,11,1,180\n780,86.4,6,11,1,180\n");
        RequireDefinitionError(
            [&multiple]() {
                ns3::LoadConstellationDefinition(multiple);
            },
            "multiple shell rows were accepted");

        const auto invalidAltitude =
            WriteCsv(outputDirectory, "altitude.csv", "0,86.4,6,11,1,180\n");
        RequireDefinitionError(
            [&invalidAltitude]() {
                ns3::LoadConstellationDefinition(invalidAltitude);
            },
            "zero altitude was accepted");

        const auto invalidPhasing =
            WriteCsv(outputDirectory, "phasing.csv", "780,86.4,6,11,6,180\n");
        RequireDefinitionError(
            [&invalidPhasing]() {
                ns3::LoadConstellationDefinition(invalidPhasing);
            },
            "out-of-range phasing was accepted");

        const auto excessive =
            WriteCsv(outputDirectory, "excessive.csv", "780,86.4,500,500,1,180\n");
        RequireDefinitionError(
            [&excessive]() {
                ns3::LoadConstellationDefinition(excessive);
            },
            "excessive satellite count was accepted");
    }
    catch (const std::exception& error)
    {
        std::cerr << "constellation definition test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
