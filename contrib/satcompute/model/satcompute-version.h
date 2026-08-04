/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_VERSION_H
#define SATCOMPUTE_VERSION_H

#include <string_view>

namespace ns3
{

/**
 * Return the version of the authoritative SatCompute scenario contract.
 *
 * @return Scenario schema version.
 */
std::string_view GetSatComputeSchemaVersion();

} // namespace ns3

#endif // SATCOMPUTE_VERSION_H
