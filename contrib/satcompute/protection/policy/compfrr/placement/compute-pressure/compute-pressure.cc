/* SPDX-License-Identifier: GPL-2.0-only */
#include "compute-pressure.h"
#include <cmath>
#include <stdexcept>
namespace ns3::protection
{
double CumulativeComputePressure(uint64_t busy, uint64_t exposure)
{
    return exposure ? static_cast<double>(busy) / exposure : 0;
}
double IdleAwareComputePressure(double global, int64_t remaining, int64_t idle)
{
    if (!(std::isfinite(global) && global >= 0 && global <= 1 && remaining > 0 && idle >= 0))
        throw std::invalid_argument("invalid Rational-U domain");
    const double horizon = static_cast<double>(remaining);
    return global * (horizon / (horizon + static_cast<double>(idle))); // No integer-sum overflow.
}


} // namespace ns3::protection
