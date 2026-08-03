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

#ifndef SATCOMPUTE_ECMP_ROUTE_CANDIDATE_H
#define SATCOMPUTE_ECMP_ROUTE_CANDIDATE_H

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <tuple>

namespace ns3 {

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

} // namespace ns3

#endif
