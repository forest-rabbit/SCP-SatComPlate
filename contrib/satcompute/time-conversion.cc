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

int
ParseExponent(std::string_view token, std::size_t& position, std::string_view fieldName)
{
    if (position == token.size() || (token[position] != 'e' && token[position] != 'E'))
    {
        return 0;
    }
    ++position;
    bool negative = false;
    if (position < token.size() && (token[position] == '+' || token[position] == '-'))
    {
        negative = token[position] == '-';
        ++position;
    }
    if (position == token.size() || token[position] < '0' || token[position] > '9')
    {
        Fail(fieldName, "contains an invalid exponent");
    }
    int exponent = 0;
    while (position < token.size() && token[position] >= '0' && token[position] <= '9')
    {
        if (exponent > 100000)
        {
            Fail(fieldName, "contains an exponent outside the supported range");
        }
        exponent = exponent * 10 + (token[position] - '0');
        ++position;
    }
    return negative ? -exponent : exponent;
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

int64_t
SatComputeDecimalSecondsToNanoseconds(std::string_view token,
                                     std::string_view fieldName,
                                     bool positive)
{
    if (token.empty() || token.front() == '-' || token.front() == '+')
    {
        Fail(fieldName, "must be a non-negative decimal number");
    }

    std::size_t position = 0;
    if (token[position] < '0' || token[position] > '9')
    {
        Fail(fieldName, "contains an invalid decimal number");
    }
    if (token[position] == '0' && position + 1 < token.size() &&
        token[position + 1] >= '0' && token[position + 1] <= '9')
    {
        Fail(fieldName, "contains a leading zero");
    }

    std::string digits;
    while (position < token.size() && token[position] >= '0' && token[position] <= '9')
    {
        digits.push_back(token[position]);
        ++position;
    }

    int fractionalDigits = 0;
    if (position < token.size() && token[position] == '.')
    {
        ++position;
        const std::size_t fractionStart = position;
        while (position < token.size() && token[position] >= '0' && token[position] <= '9')
        {
            digits.push_back(token[position]);
            ++fractionalDigits;
            ++position;
        }
        if (position == fractionStart)
        {
            Fail(fieldName, "contains an empty fractional part");
        }
    }

    const int exponent = ParseExponent(token, position, fieldName);
    if (position != token.size())
    {
        Fail(fieldName, "contains trailing characters");
    }

    const std::size_t firstNonzero = digits.find_first_not_of('0');
    if (firstNonzero == std::string::npos)
    {
        digits = "0";
    }
    else if (firstNonzero > 0)
    {
        digits.erase(0, firstNonzero);
    }

    const int64_t nanosecondPower =
        9 + static_cast<int64_t>(exponent) - static_cast<int64_t>(fractionalDigits);
    if (digits != "0" && nanosecondPower >= 0)
    {
        if (nanosecondPower > 19 ||
            digits.size() + static_cast<std::size_t>(nanosecondPower) > 19)
        {
            Fail(fieldName, "exceeds signed 64-bit nanosecond range");
        }
        digits.append(static_cast<std::size_t>(nanosecondPower), '0');
    }
    else if (digits != "0" && nanosecondPower < 0)
    {
        const int64_t divisorDigits = -nanosecondPower;
        if (divisorDigits > static_cast<int64_t>(digits.size()))
        {
            Fail(fieldName, "has precision finer than one nanosecond");
        }
        const std::size_t keep = digits.size() - static_cast<std::size_t>(divisorDigits);
        for (std::size_t index = keep; index < digits.size(); ++index)
        {
            if (digits[index] != '0')
            {
                Fail(fieldName, "has precision finer than one nanosecond");
            }
        }
        digits.resize(keep);
    }

    uint64_t nanoseconds = 0;
    const uint64_t maximum = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    for (const char character : digits)
    {
        const uint64_t digit = static_cast<uint64_t>(character - '0');
        if (nanoseconds > (maximum - digit) / 10)
        {
            Fail(fieldName, "exceeds signed 64-bit nanosecond range");
        }
        nanoseconds = nanoseconds * 10 + digit;
    }
    const int64_t parsed = static_cast<int64_t>(nanoseconds);
    if (positive && parsed == 0)
    {
        Fail(fieldName, "must be positive");
    }
    return parsed;
}

} // namespace ns3
