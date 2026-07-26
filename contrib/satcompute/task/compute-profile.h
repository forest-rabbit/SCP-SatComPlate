/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SATCOMPUTE_COMPUTE_PROFILE_H
#define SATCOMPUTE_COMPUTE_PROFILE_H

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

class SatelliteTopology;

struct ComputeNodeProfile
{
  uint32_t nodeId;
  uint64_t computeRateWorkUnitsPerSecond;
};

struct ComputeProfile
{
  std::vector<ComputeNodeProfile> nodes;
};

ComputeProfile ReadComputeProfile(const std::string& filename,
                                  const SatelliteTopology& topology,
                                  const std::string& logMode);

const ComputeNodeProfile*
FindComputeNodeProfile(const ComputeProfile& profile, uint32_t nodeId);

const ComputeNodeProfile&
GetComputeNodeProfile(const ComputeProfile& profile, uint32_t nodeId);

} // namespace ns3

#endif
