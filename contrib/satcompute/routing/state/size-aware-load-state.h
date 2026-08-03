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

#ifndef SATCOMPUTE_SIZE_AWARE_LOAD_STATE_H
#define SATCOMPUTE_SIZE_AWARE_LOAD_STATE_H

#include "../common/ecmp-route-candidate.h"
#include "size-aware-load-view.h"

#include <cstdint>
#include <map>

namespace ns3 {

struct SizeAwareLoadChange
{
  uint64_t candidateReservedBefore;
  uint64_t candidateReservedAfter;
  uint64_t totalReservedBefore;
  uint64_t totalReservedAfter;
};

class SizeAwareLoadState : public SizeAwareLoadView
{
public:
  SizeAwareLoadState();

  SizeAwareLoadChange Reserve(uint32_t nodeId,
                              const EcmpRouteCandidate& candidate,
                              uint64_t bytes);
  SizeAwareLoadChange Release(uint32_t nodeId,
                              const EcmpRouteCandidate& candidate,
                              uint64_t bytes);

  uint64_t GetReservedBytes(
    uint32_t nodeId,
    const EcmpRouteCandidate& candidate) const override;
  uint64_t GetTotalReservedBytes() const;
  uint64_t GetPeakReservedBytes() const;
  uint64_t GetPeakCandidateReservedBytes() const;
  void Clear();

private:
  struct NodeNextHopKey
  {
    uint32_t nodeId;
    Ipv4Address gateway;
    uint32_t outputInterface;

    bool operator<(const NodeNextHopKey& other) const;
  };

  std::map<NodeNextHopKey, uint64_t> m_nextHopReservedBytes;
  uint64_t m_totalReservedBytes;
  uint64_t m_peakReservedBytes;
  uint64_t m_peakCandidateReservedBytes;
};

} // namespace ns3

#endif
