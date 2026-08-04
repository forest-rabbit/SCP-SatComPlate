/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_PROFILE_H
#define SATCOMPUTE_COMPUTE_PROFILE_H

#include "../topology/satellite-endpoint-view.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ComputeProfileError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct ComputeNodeProfile
{
    uint32_t nodeId{};
    uint64_t computeRateWorkUnitsPerSecond{};
};

struct ComputeProfile
{
    std::vector<ComputeNodeProfile> nodes;
};

/** Read, validate, and canonicalize one ComputeProfile 0.1 input. */
ComputeProfile ReadComputeProfile(const std::filesystem::path& filename,
                                  const SatelliteEndpointView& endpoints);

const ComputeNodeProfile* FindComputeNodeProfile(const ComputeProfile& profile,
                                                 uint32_t nodeId);
const ComputeNodeProfile& GetComputeNodeProfile(const ComputeProfile& profile,
                                                uint32_t nodeId);

} // namespace ns3

#endif
