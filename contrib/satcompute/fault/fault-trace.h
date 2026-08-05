/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_TRACE_H
#define SATCOMPUTE_FAULT_TRACE_H

#include "fault-definition.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ComputeProfile;
class SatelliteEndpointView;

class FaultTraceError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct FaultTrace
{
    std::filesystem::path sourcePath;
    std::vector<FaultDefinition> faults;
};

/** Read, validate, and sort a deterministic fault trace by fault ID. */
FaultTrace ReadFaultTrace(const std::filesystem::path& filename,
                          int64_t simulationDurationNs,
                          const SatelliteEndpointView& endpoints,
                          const ComputeProfile* computeProfile);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_TRACE_H
