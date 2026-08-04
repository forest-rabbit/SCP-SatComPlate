/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ECMP_ROUTE_CANDIDATE_H
#define SATCOMPUTE_ECMP_ROUTE_CANDIDATE_H

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <tuple>

namespace ns3
{

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
                               destinationMask.Get()) <
               std::make_tuple(other.gateway.Get(),
                               other.outputInterface,
                               other.destination.Get(),
                               other.destinationMask.Get());
    }

    bool operator==(const EcmpRouteCandidate& other) const
    {
        return gateway == other.gateway && outputInterface == other.outputInterface &&
               destination == other.destination && destinationMask == other.destinationMask;
    }
};

} // namespace ns3

#endif
