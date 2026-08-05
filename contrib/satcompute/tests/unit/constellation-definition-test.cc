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
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
CaptureDefinitionError(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const ns3::ConstellationDefinitionError& error)
    {
        return error.what();
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

        const auto noHeader = WriteCsv(outputDirectory,
                                       "no-header.csv",
                                       "780.0,86.4,6,11,1,180\n");
        Require(ns3::LoadConstellationDefinition(noHeader).GetSatelliteCount() == 66,
                "headerless six-column shell was rejected");

        const auto commentsAndBlanks = WriteCsv(
            outputDirectory,
            "comments-and-blanks.csv",
            "\n# comment\n\t\n"
            "altitudeKm,inclinationDegrees,numberOfPlanes,numberOfSatellitesPerPlane,"
            "phasingFactor,raanSpanDeg\n"
            "# another comment\n780.0,86.4,6,11,1,180\n");
        Require(ns3::LoadConstellationDefinition(commentsAndBlanks).GetSatelliteCount() == 66,
                "comments or blank lines changed strict parsing");

        const auto missing = WriteCsv(outputDirectory, "missing.csv", "# no shell row\n");
        CaptureDefinitionError(
            [&missing]() {
                ns3::LoadConstellationDefinition(missing);
            },
            "missing shell row was accepted");

        const auto multiple =
            WriteCsv(outputDirectory, "multiple.csv", "780,86.4,6,11,1,180\n780,86.4,6,11,1,180\n");
        CaptureDefinitionError(
            [&multiple]() {
                ns3::LoadConstellationDefinition(multiple);
            },
            "multiple shell rows were accepted");

        const auto trailingGarbage = WriteCsv(outputDirectory,
                                              "trailing-garbage.csv",
                                              "780,86.4,6,11,1,180\ninvalid data\n");
        const std::string trailingError = CaptureDefinitionError(
            [&trailingGarbage]() {
                ns3::LoadConstellationDefinition(trailingGarbage);
            },
            "garbage after the shell row was accepted");
        Require(trailingError.find(trailingGarbage.string()) != std::string::npos &&
                    trailingError.find("line 2") != std::string::npos,
                "strict parse error omitted path or line number");

        const auto misspelledHeader = WriteCsv(
            outputDirectory,
            "misspelled-header.csv",
            "altitudeKm,inclinationDegrees,planes,numberOfSatellitesPerPlane,"
            "phasingFactor,raanSpanDeg\n780,86.4,6,11,1,180\n");
        CaptureDefinitionError(
            [&misspelledHeader]() {
                ns3::LoadConstellationDefinition(misspelledHeader);
            },
            "misspelled header was accepted");

        for (const auto& [filename, row] :
             std::vector<std::pair<std::string, std::string>>{
                 {"five-columns.csv", "780,86.4,6,11,1\n"},
                 {"seven-columns.csv", "780,86.4,6,11,1,180,extra\n"}})
        {
            const auto malformed = WriteCsv(outputDirectory, filename, row);
            CaptureDefinitionError(
                [&malformed]() {
                    ns3::LoadConstellationDefinition(malformed);
                },
                "non-six-column shell was accepted");
        }

        for (const auto& [filename, row] :
             std::vector<std::pair<std::string, std::string>>{
                 {"fractional-planes.csv", "780,86.4,6.5,11,1,180\n"},
                 {"fractional-satellites.csv", "780,86.4,6,11.5,1,180\n"},
                 {"fractional-phasing.csv", "780,86.4,6,11,1.5,180\n"}})
        {
            const auto fractional = WriteCsv(outputDirectory, filename, row);
            CaptureDefinitionError(
                [&fractional]() {
                    ns3::LoadConstellationDefinition(fractional);
                },
                "fractional integer field was accepted");
        }

        const auto invalidAltitude =
            WriteCsv(outputDirectory, "altitude.csv", "0,86.4,6,11,1,180\n");
        CaptureDefinitionError(
            [&invalidAltitude]() {
                ns3::LoadConstellationDefinition(invalidAltitude);
            },
            "zero altitude was accepted");

        const auto invalidPhasing =
            WriteCsv(outputDirectory, "phasing.csv", "780,86.4,6,11,6,180\n");
        CaptureDefinitionError(
            [&invalidPhasing]() {
                ns3::LoadConstellationDefinition(invalidPhasing);
            },
            "out-of-range phasing was accepted");

        const auto excessive =
            WriteCsv(outputDirectory, "excessive.csv", "780,86.4,500,500,1,180\n");
        CaptureDefinitionError(
            [&excessive]() {
                ns3::LoadConstellationDefinition(excessive);
            },
            "excessive satellite count was accepted");

        const auto invalidUtf8 = WriteCsv(outputDirectory,
                                          "invalid-utf8.csv",
                                          std::string("# ") + static_cast<char>(0xff) +
                                              "\n780,86.4,6,11,1,180\n");
        CaptureDefinitionError(
            [&invalidUtf8]() {
                ns3::LoadConstellationDefinition(invalidUtf8);
            },
            "invalid UTF-8 was accepted");

        const long double exactLimit =
            ns3::CalculateClearanceLimitedMaxIslDistanceMeters(
                definition.shell.alt,
                ns3::SATCOMPUTE_MINIMUM_ISL_RAY_ALTITUDE_METERS);
        const long double allowedLimit = std::floor(exactLimit);
        Require(allowedLimit == 6171353.0L,
                "ns-3.48 clearance limit differs from the expected default");
        ns3::ValidateMaxIslDistanceAgainstOrbit(definition.shell, 6171353.0L);
        ns3::ValidateMaxIslDistanceAgainstOrbit(definition.shell, allowedLimit);

        const std::string clearanceError = CaptureDefinitionError(
            [&definition, allowedLimit]() {
                ns3::ValidateMaxIslDistanceAgainstOrbit(definition.shell,
                                                        allowedLimit + 1.0L);
            },
            "distance one meter above the clearance limit was accepted");
        Require(clearanceError.find("6171354 m") != std::string::npos &&
                    clearanceError.find("6171353 m") != std::string::npos &&
                    clearanceError.find("clearance limit") != std::string::npos,
                "clearance error omitted actual value, allowed limit, or units");

        const ns3::LeoOrbitalShell lowerOrbit(400.0, 86.4, 6, 11, 1, 180);
        CaptureDefinitionError(
            [&lowerOrbit]() {
                ns3::ValidateMaxIslDistanceAgainstOrbit(lowerOrbit, 6171353.0L);
            },
            "lower orbit accepted the old distance limit");

        for (const long double altitude :
             {-1.0L,
              ns3::SATCOMPUTE_MINIMUM_ISL_RAY_ALTITUDE_METERS / 1000.0L,
              std::numeric_limits<long double>::infinity(),
              std::numeric_limits<long double>::quiet_NaN()})
        {
            CaptureDefinitionError(
                [altitude]() {
                    ns3::CalculateClearanceLimitedMaxIslDistanceMeters(
                        altitude,
                        ns3::SATCOMPUTE_MINIMUM_ISL_RAY_ALTITUDE_METERS);
                },
                "invalid orbit altitude was accepted by clearance calculation");
        }
        for (const long double distance :
             {-1.0L,
              std::numeric_limits<long double>::infinity(),
              std::numeric_limits<long double>::quiet_NaN()})
        {
            CaptureDefinitionError(
                [&definition, distance]() {
                    ns3::ValidateMaxIslDistanceAgainstOrbit(definition.shell, distance);
                },
                "invalid max ISL distance was accepted");
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "constellation definition test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
