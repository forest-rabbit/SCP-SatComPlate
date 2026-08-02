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

#ifndef SATCOMPUTE_ECMP_ROUTE_SELECTOR_H
#define SATCOMPUTE_ECMP_ROUTE_SELECTOR_H

#include "ecmp-flow-key.h"
#include "fnv1a64.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace ns3 {

enum class EcmpRouteSelectionMode
{
  GLOBAL_FIRST,
  HASH_PER_FLOW,
  HRW_PER_FLOW,
  SIZE_AWARE_HRW,
  CAPACITY_WEIGHTED_HRW,
  CAPACITY_AWARE_HRW
};

struct EcmpRouteCandidate
{
  Ipv4Address gateway;
  uint32_t outputInterface;
  Ipv4Address destination;
  Ipv4Mask destinationMask;

  bool operator<(const EcmpRouteCandidate& other) const
  {
    return std::make_tuple(gateway.Get(),
                           outputInterface,
                           destination.Get(),
                           destinationMask.Get())
           < std::make_tuple(other.gateway.Get(),
                             other.outputInterface,
                             other.destination.Get(),
                             other.destinationMask.Get());
  }

  bool operator==(const EcmpRouteCandidate& other) const
  {
    return gateway == other.gateway
           && outputInterface == other.outputInterface
           && destination == other.destination
           && destinationMask == other.destinationMask;
  }
};

struct EcmpHrwSelection
{
  uint32_t candidateIndex;
  uint64_t score;
};

struct EcmpHrwRank
{
  uint32_t candidateIndex;
  uint64_t score;
};

inline std::array<uint8_t, 37>
EncodeEcmpHrwKey(uint64_t hashSeed,
                 const EcmpFlowKey& flowKey,
                 const EcmpRouteCandidate& candidate)
{
  std::array<uint8_t, 37> bytes = {};
  std::array<uint8_t, 21> flowBytes =
    EncodeEcmpFlowKey(hashSeed, flowKey);
  std::copy(flowBytes.begin(), flowBytes.end(), bytes.begin());

  std::size_t offset = flowBytes.size();
  auto appendUint32 = [&bytes, &offset](uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
      {
        bytes[offset++] = static_cast<uint8_t>(value >> shift);
      }
  };
  appendUint32(candidate.gateway.Get());
  appendUint32(candidate.outputInterface);
  appendUint32(candidate.destination.Get());
  appendUint32(candidate.destinationMask.Get());
  return bytes;
}

inline EcmpHrwSelection
SelectEcmpHrwRoute(uint64_t hashSeed,
                   const EcmpFlowKey& flowKey,
                   const std::vector<EcmpRouteCandidate>& candidates)
{
  EcmpHrwSelection selected = {
    0,
    Fnv1a64(EncodeEcmpHrwKey(hashSeed, flowKey, candidates[0]))
  };
  for (uint32_t index = 1; index < candidates.size(); ++index)
    {
      uint64_t score =
        Fnv1a64(EncodeEcmpHrwKey(hashSeed, flowKey, candidates[index]));
      if (score > selected.score
          || (score == selected.score
              && candidates[index] < candidates[selected.candidateIndex]))
        {
          selected.candidateIndex = index;
          selected.score = score;
        }
    }
  return selected;
}

inline std::vector<EcmpHrwRank>
RankEcmpHrwRoutes(uint64_t hashSeed,
                  const EcmpFlowKey& flowKey,
                  const std::vector<EcmpRouteCandidate>& candidates)
{
  std::vector<EcmpHrwRank> ranking;
  ranking.reserve(candidates.size());
  for (uint32_t index = 0; index < candidates.size(); ++index)
    {
      EcmpHrwRank rank = {
        index,
        Fnv1a64(EncodeEcmpHrwKey(hashSeed, flowKey, candidates[index]))
      };
      ranking.push_back(rank);
    }
  std::stable_sort(
    ranking.begin(),
    ranking.end(),
    [&candidates](const EcmpHrwRank& left, const EcmpHrwRank& right) {
      return left.score > right.score
             || (left.score == right.score
                 && candidates[left.candidateIndex]
                      < candidates[right.candidateIndex]);
    });
  return ranking;
}

} // namespace ns3

#endif
