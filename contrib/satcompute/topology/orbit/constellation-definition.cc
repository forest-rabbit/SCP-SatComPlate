/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "constellation-definition.h"

#include "ns3/csv-reader.h"

#include <cmath>
#include <limits>
#include <string>

namespace ns3
{

namespace
{

constexpr uint32_t MAX_SATELLITES = 99999;

[[noreturn]] void
Fail(const std::filesystem::path& path, const std::string& message)
{
    throw ConstellationDefinitionError(path.string() + ": " + message);
}

bool
ReadShellRow(const CsvReader& csv, LeoOrbitalShell& shell)
{
    if (csv.ColumnCount() < 4 || csv.ColumnCount() > 6)
    {
        return false;
    }
    bool valid = csv.GetValue(0, shell.alt);
    valid = csv.GetValue(1, shell.inc) && valid;
    valid = csv.GetValue(2, shell.planes) && valid;
    valid = csv.GetValue(3, shell.sats) && valid;
    if (!valid)
    {
        return false;
    }
    if (csv.ColumnCount() >= 5 && !csv.GetValue(4, shell.phasing))
    {
        return false;
    }
    if (csv.ColumnCount() >= 6 && !csv.GetValue(5, shell.raanSpanDeg))
    {
        return false;
    }
    return true;
}

void
ValidateShell(const std::filesystem::path& path, const LeoOrbitalShell& shell)
{
    if (!std::isfinite(shell.alt) || shell.alt <= 0.0)
    {
        Fail(path, "altitudeKm must be finite and greater than zero");
    }
    if (!std::isfinite(shell.inc) || shell.inc < 0.0 || shell.inc >= 180.0)
    {
        Fail(path, "inclinationDegrees must be in [0, 180)");
    }
    if (shell.planes == 0 || shell.sats == 0)
    {
        Fail(path, "plane and satellite counts must be greater than zero");
    }
    if (!std::isfinite(shell.phasing) || shell.phasing < 0.0 ||
        std::floor(shell.phasing) != shell.phasing || shell.phasing >= shell.planes)
    {
        Fail(path, "phasingFactor must be an integer in [0, planes - 1]");
    }
    if (!std::isfinite(shell.raanSpanDeg) || shell.raanSpanDeg <= 0.0 ||
        shell.raanSpanDeg > 360.0)
    {
        Fail(path, "raanSpanDeg must be in (0, 360]");
    }
    if (shell.planes > std::numeric_limits<uint32_t>::max() ||
        shell.sats > std::numeric_limits<uint32_t>::max() ||
        shell.planes > MAX_SATELLITES / shell.sats)
    {
        Fail(path, "total satellite count must not exceed 99999");
    }
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

    CsvReader csv(sourcePath.string());
    LeoOrbitalShell parsed;
    uint32_t validRows = 0;
    while (csv.FetchNextRow())
    {
        if (csv.IsBlankRow())
        {
            continue;
        }
        LeoOrbitalShell candidate;
        if (!ReadShellRow(csv, candidate))
        {
            // The native helper permits a descriptive header. Any other
            // non-numeric row is ignored in exactly the same way.
            continue;
        }
        ValidateShell(sourcePath, candidate);
        parsed = candidate;
        ++validRows;
    }
    if (validRows != 1)
    {
        Fail(sourcePath, "SatCompute requires exactly one valid LEO shell row");
    }
    return {sourcePath, parsed};
}

} // namespace ns3
