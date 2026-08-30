/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_PARAMETER_VALIDATOR_H
#define SATCOMPUTE_FAULT_PARAMETER_VALIDATOR_H

#include "fault-para.h"

#include <stdexcept>

namespace ns3
{

/** Invalid in-memory fault parameter set. */
class FaultParameterError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Validate fault parameters before an online run or calibration.
 *
 * @param parameters Parameters to validate.
 * @throws FaultParameterError if a value or cross-field relation is invalid.
 */
void ValidateFaultParameters(const FaultParameters& parameters);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_PARAMETER_VALIDATOR_H
