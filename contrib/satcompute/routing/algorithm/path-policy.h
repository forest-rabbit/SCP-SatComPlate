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

#ifndef SATCOMPUTE_PATH_POLICY_H
#define SATCOMPUTE_PATH_POLICY_H

#include "capacity-aware-path-types.h"
#include "../common/ecmp-flow-key.h"

#include <cstdint>

namespace ns3 {

struct PathSelectionContext
{
  EcmpFlowKey flowKey;
  uint32_t sourceSatelliteId;
  uint32_t destinationSatelliteId;
  uint64_t hashSeed;
};

class PathPolicy
{
public:
  virtual ~PathPolicy()
  {
  }

  virtual bool FindPath(const PathSelectionContext& context,
                        CapacityAwarePath& path) const = 0;
};

} // namespace ns3

#endif
