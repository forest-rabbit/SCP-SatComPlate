/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "constellation-definition.h"

#include "ns3/geographic-positions.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ns3
{

namespace
{

constexpr uint32_t MAX_SATELLITES = 99999;
constexpr std::string_view REQUIRED_HEADER =
    "altitudeKm,inclinationDegrees,numberOfPlanes,numberOfSatellitesPerPlane,"
    "phasingFactor,raanSpanDeg";

[[noreturn]] void
Fail(const std::filesystem::path& path, const std::string& message)
{
    throw ConstellationDefinitionError(path.string() + ": " + message);
}

[[noreturn]] void
FailLine(const std::filesystem::path& path,
         std::size_t lineNumber,
         const std::string& message)
{
    Fail(path, "line " + std::to_string(lineNumber) + ": " + message);
}

bool
IsContinuationByte(unsigned char value)
{
    return value >= 0x80 && value <= 0xbf;
}

void
ValidateUtf8(const std::filesystem::path& path, std::string_view contents)
{
    std::size_t lineNumber = 1;
    for (std::size_t index = 0; index < contents.size();)
    {
        const unsigned char first = static_cast<unsigned char>(contents[index]);
        if (first <= 0x7f)
        {
            if (first == '\n')
            {
                ++lineNumber;
            }
            ++index;
            continue;
        }

        std::size_t length = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            length = 2;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            length = 3;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            length = 4;
        }
        else
        {
            FailLine(path, lineNumber, "input is not valid UTF-8");
        }

        if (index + length > contents.size())
        {
            FailLine(path, lineNumber, "input ends inside a UTF-8 sequence");
        }
        for (std::size_t offset = 1; offset < length; ++offset)
        {
            if (!IsContinuationByte(static_cast<unsigned char>(contents[index + offset])))
            {
                FailLine(path, lineNumber, "input is not valid UTF-8");
            }
        }

        const unsigned char second = static_cast<unsigned char>(contents[index + 1]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f))
        {
            FailLine(path, lineNumber, "input contains an invalid UTF-8 code point");
        }
        index += length;
    }
}

bool
IsBlankLine(std::string_view line)
{
    return std::all_of(line.begin(), line.end(), [](char character) {
        return character == ' ' || character == '\t' || character == '\v' ||
               character == '\f';
    });
}

std::vector<std::string_view>
SplitColumns(std::string_view line)
{
    std::vector<std::string_view> columns;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t comma = line.find(',', start);
        columns.push_back(line.substr(start, comma - start));
        if (comma == std::string_view::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return columns;
}

double
ParseDouble(const std::filesystem::path& path,
            std::size_t lineNumber,
            std::string_view token,
            std::string_view fieldName)
{
    double value = 0.0;
    const auto result = std::from_chars(token.data(),
                                        token.data() + token.size(),
                                        value,
                                        std::chars_format::general);
    if (token.empty() || result.ec != std::errc() ||
        result.ptr != token.data() + token.size())
    {
        FailLine(path,
                 lineNumber,
                 std::string(fieldName) + " must be a complete decimal number");
    }
    return value;
}

std::size_t
ParseUnsignedInteger(const std::filesystem::path& path,
                     std::size_t lineNumber,
                     std::string_view token,
                     std::string_view fieldName)
{
    std::size_t value = 0;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (token.empty() || result.ec != std::errc() ||
        result.ptr != token.data() + token.size())
    {
        FailLine(path,
                 lineNumber,
                 std::string(fieldName) + " must be an unsigned integer");
    }
    return value;
}

void
ValidateShell(const std::filesystem::path& path,
              std::size_t lineNumber,
              const LeoOrbitalShell& shell)
{
    if (!std::isfinite(shell.alt) || shell.alt <= 0.0)
    {
        FailLine(path, lineNumber, "altitudeKm must be finite and greater than zero");
    }
    if (!std::isfinite(shell.inc) || shell.inc < 0.0 || shell.inc >= 180.0)
    {
        FailLine(path, lineNumber, "inclinationDegrees must be in [0, 180)");
    }
    if (shell.planes == 0 || shell.sats == 0)
    {
        FailLine(path,
                 lineNumber,
                 "plane and satellite counts must be greater than zero");
    }
    if (!std::isfinite(shell.phasing) || shell.phasing < 0.0 ||
        std::floor(shell.phasing) != shell.phasing || shell.phasing >= shell.planes)
    {
        FailLine(path,
                 lineNumber,
                 "phasingFactor must be an integer in [0, planes - 1]");
    }
    if (!std::isfinite(shell.raanSpanDeg) || shell.raanSpanDeg <= 0.0 ||
        shell.raanSpanDeg > 360.0)
    {
        FailLine(path, lineNumber, "raanSpanDeg must be in (0, 360]");
    }
    if (shell.planes > std::numeric_limits<uint32_t>::max() ||
        shell.sats > std::numeric_limits<uint32_t>::max() ||
        shell.planes > MAX_SATELLITES / shell.sats)
    {
        FailLine(path, lineNumber, "total satellite count must not exceed 99999");
    }
}

LeoOrbitalShell
ParseShellLine(const std::filesystem::path& path,
               std::size_t lineNumber,
               std::string_view line)
{
    const std::vector<std::string_view> columns = SplitColumns(line);
    if (columns.size() != 6)
    {
        FailLine(path, lineNumber, "expected exactly six shell columns");
    }

    LeoOrbitalShell shell;
    shell.alt = ParseDouble(path, lineNumber, columns[0], "altitudeKm");
    shell.inc = ParseDouble(path, lineNumber, columns[1], "inclinationDegrees");
    shell.planes = ParseUnsignedInteger(path,
                                        lineNumber,
                                        columns[2],
                                        "numberOfPlanes");
    shell.sats = ParseUnsignedInteger(path,
                                      lineNumber,
                                      columns[3],
                                      "numberOfSatellitesPerPlane");
    shell.phasing = static_cast<double>(
        ParseUnsignedInteger(path, lineNumber, columns[4], "phasingFactor"));
    shell.raanSpanDeg = ParseDouble(path, lineNumber, columns[5], "raanSpanDeg");
    ValidateShell(path, lineNumber, shell);
    return shell;
}

} // namespace

uint32_t
ConstellationDefinition::GetSatelliteCount() const
{
    return static_cast<uint32_t>(shell.planes * shell.sats);
}

ConstellationDefinition
LoadConstellationDefinition(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path sourcePath = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(sourcePath))
    {
        throw ConstellationDefinitionError(
            "constellation must be an existing regular CSV file: " + path.string());
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input.is_open())
    {
        Fail(sourcePath, "cannot open constellation CSV");
    }
    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (input.bad())
    {
        Fail(sourcePath, "cannot read constellation CSV");
    }
    ValidateUtf8(sourcePath, contents);
    if (contents.starts_with("\xef\xbb\xbf"))
    {
        contents.erase(0, 3);
    }

    std::istringstream lines(contents);
    std::string line;
    std::size_t lineNumber = 0;
    bool headerSeen = false;
    bool shellSeen = false;
    LeoOrbitalShell shell;
    while (std::getline(lines, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (IsBlankLine(line) || (!line.empty() && line.front() == '#'))
        {
            continue;
        }
        if (line == REQUIRED_HEADER)
        {
            if (headerSeen || shellSeen)
            {
                FailLine(sourcePath,
                         lineNumber,
                         "header may appear at most once before shell data");
            }
            headerSeen = true;
            continue;
        }
        if (shellSeen)
        {
            FailLine(sourcePath,
                     lineNumber,
                     "unexpected content after the single shell data row");
        }
        shell = ParseShellLine(sourcePath, lineNumber, line);
        shellSeen = true;
    }
    if (!shellSeen)
    {
        FailLine(sourcePath,
                 lineNumber + 1,
                 "expected exactly one six-column shell data row before end of file");
    }
    return {sourcePath, shell};
}

long double
CalculateClearanceLimitedMaxIslDistanceMeters(long double altitudeKm,
                                               long double minimumRayAltitudeMeters)
{
    if (!std::isfinite(altitudeKm) || altitudeKm < 0.0L)
    {
        throw ConstellationDefinitionError(
            "altitudeKm must be a finite non-negative number for clearance validation");
    }
    if (!std::isfinite(minimumRayAltitudeMeters) || minimumRayAltitudeMeters < 0.0L)
    {
        throw ConstellationDefinitionError(
            "minimum ray altitude must be finite and non-negative meters");
    }

    const long double earthRadiusMeters =
        static_cast<long double>(GeographicPositions::EARTH_SPHERE_RADIUS);
    const long double orbitalRadiusMeters = earthRadiusMeters + altitudeKm * 1000.0L;
    const long double clearanceRadiusMeters = earthRadiusMeters + minimumRayAltitudeMeters;
    if (orbitalRadiusMeters <= clearanceRadiusMeters)
    {
        throw ConstellationDefinitionError(
            "orbital altitude must exceed the minimum ray altitude");
    }
    return 2.0L * std::sqrt(orbitalRadiusMeters * orbitalRadiusMeters -
                            clearanceRadiusMeters * clearanceRadiusMeters);
}

void
ValidateMaxIslDistanceAgainstOrbit(const LeoOrbitalShell& shell,
                                   long double configuredMaxIslDistanceMeters)
{
    if (!std::isfinite(configuredMaxIslDistanceMeters) ||
        configuredMaxIslDistanceMeters <= 0.0L)
    {
        throw ConstellationDefinitionError(
            "maxIslDistance must be a finite positive number of meters");
    }
    const long double exactLimit = CalculateClearanceLimitedMaxIslDistanceMeters(
        static_cast<long double>(shell.alt),
        SATCOMPUTE_MINIMUM_ISL_RAY_ALTITUDE_METERS);
    const long double allowedMeters = std::floor(exactLimit);
    if (configuredMaxIslDistanceMeters > allowedMeters)
    {
        std::ostringstream message;
        message << std::fixed << std::setprecision(0)
                << "maxIslDistance=" << configuredMaxIslDistanceMeters
                << " m exceeds clearance limit=" << allowedMeters
                << " m for altitude=" << static_cast<long double>(shell.alt)
                << " km and minimum ray altitude="
                << SATCOMPUTE_MINIMUM_ISL_RAY_ALTITUDE_METERS << " m";
        throw ConstellationDefinitionError(message.str());
    }
}

} // namespace ns3
