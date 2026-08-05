/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "constellation-definition.h"

#include "../../para.h"
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

constexpr uint32_t MAX_SATELLITES = 99999;

[[noreturn]] void
Fail(std::string_view field, std::string_view message)
{
    throw ConstellationDefinitionError(std::string(field) + " " + std::string(message));
}

const Json&
GetField(const Json& object, std::string_view field)
{
    const auto iterator = object.find(std::string(field));
    if (iterator == object.end())
    {
        Fail(field, "is missing");
    }
    return *iterator;
}

void
RequireFields(const Json& root, std::initializer_list<std::string_view> fields)
{
    if (!root.is_object())
    {
        Fail("constellation", "must be an object");
    }
    std::set<std::string> expected;
    for (const std::string_view field : fields)
    {
        expected.emplace(field);
    }
    std::set<std::string> actual;
    for (const auto& item : root.items())
    {
        actual.emplace(item.key());
    }
    if (actual == expected)
    {
        return;
    }

    std::ostringstream message;
    message << "fields differ: missing=[";
    bool first = true;
    for (const std::string& field : expected)
    {
        if (!actual.contains(field))
        {
            message << (first ? "" : ",") << field;
            first = false;
        }
    }
    message << "], unknown=[";
    first = true;
    for (const std::string& field : actual)
    {
        if (!expected.contains(field))
        {
            message << (first ? "" : ",") << field;
            first = false;
        }
    }
    message << "]";
    Fail("constellation", message.str());
}

std::string
RequireString(const Json& value, std::string_view field)
{
    if (!value.is_string())
    {
        Fail(field, "must be a string");
    }
    const std::string parsed = value.get<std::string>();
    if (parsed.empty())
    {
        Fail(field, "must not be empty");
    }
    return parsed;
}

std::string
RequireToken(const Json& value, std::string_view field)
{
    const std::string parsed = RequireString(value, field);
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    if (!std::regex_match(parsed, pattern))
    {
        Fail(field, "must be a filesystem-safe token");
    }
    return parsed;
}

uint32_t
RequirePositiveUint32(const Json& value, std::string_view field)
{
    uint64_t parsed = 0;
    if (value.is_number_unsigned())
    {
        parsed = value.get<uint64_t>();
    }
    else if (value.is_number_integer() && !value.is_boolean())
    {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue <= 0)
        {
            Fail(field, "must be positive");
        }
        parsed = static_cast<uint64_t>(signedValue);
    }
    else
    {
        Fail(field, "must be an integer");
    }
    if (parsed == 0 || parsed > MAX_SATELLITES)
    {
        Fail(field, "is outside the range 1..99999");
    }
    return static_cast<uint32_t>(parsed);
}

long double
RequireNumber(const Json& value,
              std::string_view field,
              long double minimum,
              bool minimumExclusive,
              long double maximum,
              bool maximumExclusive)
{
    if (!value.is_number())
    {
        Fail(field, "must be a number");
    }
    const long double parsed = value.get<long double>();
    if (!std::isfinite(parsed))
    {
        Fail(field, "must be finite");
    }
    if ((minimumExclusive && parsed <= minimum) || (!minimumExclusive && parsed < minimum) ||
        (maximumExclusive && parsed >= maximum) || (!maximumExclusive && parsed > maximum))
    {
        Fail(field, "is outside its supported range");
    }
    return parsed;
}

Json
ReadJson(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        throw ConstellationDefinitionError("cannot open constellation: " + path.string());
    }
    try
    {
        return Json::parse(input, nullptr, true, false);
    }
    catch (const std::exception& error)
    {
        throw ConstellationDefinitionError("cannot parse constellation " + path.string() +
                                           ": " + error.what());
    }
}

} // namespace

uint32_t
ConstellationDefinition::GetSatelliteCount() const
{
    return numOrbits * satellitesPerOrbit;
}

ConstellationDefinition
LoadConstellationDefinition(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path sourcePath = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(sourcePath))
    {
        throw ConstellationDefinitionError(
            "constellation must be an existing regular file: " + path.string());
    }

    const Json root = ReadJson(sourcePath);
    RequireFields(root,
                  {"schema_version",
                   "constellation_name",
                   "constellation_pattern",
                   "num_orbits",
                   "satellites_per_orbit",
                   "altitude_m",
                   "inclination_deg",
                   "phase_diff",
                   "orbit_epoch_offset_s"});

    ConstellationDefinition definition{};
    definition.schemaVersion = RequireString(GetField(root, "schema_version"), "schema_version");
    if (definition.schemaVersion != "0.1")
    {
        Fail("schema_version", "must be 0.1");
    }
    definition.sourcePath = sourcePath;
    definition.constellationName =
        RequireToken(GetField(root, "constellation_name"), "constellation_name");
    definition.constellationPattern =
        RequireString(GetField(root, "constellation_pattern"), "constellation_pattern");
    if (definition.constellationPattern != "walker-star" &&
        definition.constellationPattern != "walker-delta")
    {
        Fail("constellation_pattern", "must be walker-star or walker-delta");
    }
    definition.numOrbits =
        RequirePositiveUint32(GetField(root, "num_orbits"), "num_orbits");
    definition.satellitesPerOrbit = RequirePositiveUint32(
        GetField(root, "satellites_per_orbit"),
        "satellites_per_orbit");
    if (static_cast<uint64_t>(definition.numOrbits) * definition.satellitesPerOrbit >
        MAX_SATELLITES)
    {
        Fail("constellation", "total satellite count must not exceed 99999");
    }
    definition.altitudeM = RequireNumber(GetField(root, "altitude_m"),
                                         "altitude_m",
                                         0.0L,
                                         true,
                                         std::numeric_limits<long double>::max(),
                                         false);
    definition.inclinationDeg = RequireNumber(GetField(root, "inclination_deg"),
                                              "inclination_deg",
                                              0.0L,
                                              false,
                                              180.0L,
                                              true);
    const Json& phaseDiff = GetField(root, "phase_diff");
    if (!phaseDiff.is_boolean())
    {
        Fail("phase_diff", "must be a boolean");
    }
    definition.phaseDiff = phaseDiff.get<bool>();

    const long double epochSeconds = RequireNumber(GetField(root, "orbit_epoch_offset_s"),
                                                   "orbit_epoch_offset_s",
                                                   0.0L,
                                                   false,
                                                   std::numeric_limits<long double>::max(),
                                                   false);
    try
    {
        definition.orbitEpochOffsetNs = SatComputeSecondsToNanoseconds(
            static_cast<double>(epochSeconds),
            "orbit_epoch_offset_s");
    }
    catch (const SatComputeConfigError& error)
    {
        throw ConstellationDefinitionError(error.what());
    }
    return definition;
}

} // namespace ns3
