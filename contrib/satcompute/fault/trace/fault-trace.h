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

constexpr uint32_t FAULT_TRACE_SCHEMA_VERSION = 2;

class FaultTraceError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct FaultTrace
{
    uint32_t schemaVersion{FAULT_TRACE_SCHEMA_VERSION};
    std::filesystem::path sourcePath;
    std::vector<FaultDefinition> faults;
};

/** Read, validate, and sort a deterministic fault trace by fault ID. */
FaultTrace ReadFaultTrace(const std::filesystem::path& filename,
                          int64_t simulationDurationNs,
                          const SatelliteEndpointView& endpoints,
                          const ComputeProfile* computeProfile);

/** Write a canonical unified Fault Trace v2 file. */
void WriteFaultTraceV2(const std::filesystem::path& filename, const FaultTrace& trace);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_TRACE_H
