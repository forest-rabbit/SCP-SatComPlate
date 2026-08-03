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

#ifndef SATCOMPUTE_CAPACITY_RESERVATION_STATE_H
#define SATCOMPUTE_CAPACITY_RESERVATION_STATE_H

#include "../algorithm/capacity-aware-path-types.h"
#include "../algorithm/capacity-aware-path-view.h"

#include <cstdint>
#include <map>
#include <utility>

namespace ns3 {

struct CapacityAwareRuntimeSummary
{
  uint64_t activePathCountAtEnd;
  uint64_t reservedDirectedLinkCountAtEnd;
  uint64_t totalReservedRateBpsAtEnd;
  uint64_t pendingTransferCountAtEnd;

  CapacityAwareRuntimeSummary()
    : activePathCountAtEnd(0),
      reservedDirectedLinkCountAtEnd(0),
      totalReservedRateBpsAtEnd(0),
      pendingTransferCountAtEnd(0)
  {
  }
};

class CapacityReservationState
{
public:
  uint64_t GetResidualRateBps(const CapacityAwarePathHop& hop) const;
  bool HasActivePath(uint64_t transferId) const;
  bool IsActivePathValid(
    uint64_t transferId,
    uint32_t destinationSatelliteId,
    const CapacityAwarePathView& pathView) const;
  CapacityAwareRuntimeSummary CollectSummary() const;
  void Reserve(uint64_t transferId, const CapacityAwarePath& path);
  void Release(uint64_t transferId);

private:
  typedef std::pair<uint32_t, uint32_t> DirectedLinkKey;

  std::map<DirectedLinkKey, uint64_t> m_reservedRateBps;
  std::map<uint64_t, CapacityAwarePath> m_activePaths;
};

} // namespace ns3

#endif
