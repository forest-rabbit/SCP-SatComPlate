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

constexpr uint32_t FAULT_TRACE_SCHEMA_VERSION = 2;

class FaultTraceError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct FaultTrace
{
    uint32_t schemaVersion{FAULT_TRACE_SCHEMA_VERSION};
    std::vector<FaultDefinition> faults;
};

/** Write a canonical unified Fault Trace v2 file. */
void WriteFaultTraceV2(const std::filesystem::path& filename, const FaultTrace& trace);

/** Read frozen v2 evidence for execution validation, never for online prediction. */
FaultTrace ReadValidationFaultTrace(const std::filesystem::path& filename,
                                    const std::vector<uint32_t>& satelliteIds,
                                    int64_t durationNs);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_TRACE_H
