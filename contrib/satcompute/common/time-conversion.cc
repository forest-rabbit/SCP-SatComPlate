/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "time-conversion.h"

#include <cmath>
#include <limits>
#include <string>

namespace ns3
{

namespace
{

[[noreturn]] void
Fail(std::string_view fieldName, std::string_view message)
{
    throw SatComputeTimeError(std::string(fieldName) + " " + std::string(message));
}

} // namespace

int64_t
SatComputeSecondsToNanoseconds(double seconds, std::string_view fieldName)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        Fail(fieldName, "must be a finite non-negative number of seconds");
    }

    constexpr long double NANOSECONDS_PER_SECOND = 1000000000.0L;
    const long double nanoseconds =
        static_cast<long double>(seconds) * NANOSECONDS_PER_SECOND;
    const long double roundedNanoseconds = std::round(nanoseconds);
    if (roundedNanoseconds > static_cast<long double>(std::numeric_limits<int64_t>::max()))
    {
        Fail(fieldName, "exceeds the int64 nanosecond range");
    }
    return static_cast<int64_t>(roundedNanoseconds);
}

} // namespace ns3
