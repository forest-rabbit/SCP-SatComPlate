/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TASK_TRACE_H
#define SATCOMPUTE_TASK_TRACE_H

#include "compute-profile.h"
#include "compute-task.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ns3
{

class TaskTraceError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct TaskTrace
{
    std::vector<TaskDefinition> tasks;
};

/** Read, validate, canonicalize, and derive stable transfer IDs. */
TaskTrace ReadTaskTrace(const std::filesystem::path& filename,
                        int64_t simulationDurationNs,
                        const SatelliteEndpointView& endpoints,
                        const ComputeProfile& computeProfile);

} // namespace ns3

#endif
