/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_ENDPOINT_VIEW_H
#define SATCOMPUTE_SATELLITE_ENDPOINT_VIEW_H

#include "ns3/ipv4-address.h"

#include <cstdint>

namespace ns3
{

/** Read-only stable satellite identity and IPv4 service-address view. */
class SatelliteEndpointView
{
  public:
    virtual ~SatelliteEndpointView()
    {
    }

    virtual bool HasSatelliteId(uint32_t satelliteId) const = 0;
    virtual Ipv4Address GetServiceAddress(uint32_t satelliteId) const = 0;
};

} // namespace ns3

#endif
