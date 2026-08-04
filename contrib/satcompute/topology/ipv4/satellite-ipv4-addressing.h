/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_IPV4_ADDRESSING_H
#define SATCOMPUTE_SATELLITE_IPV4_ADDRESSING_H

#include "../satellite-id-map.h"

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <map>
#include <stdexcept>

namespace ns3
{

class SatelliteIpv4AddressingError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Assign one deterministic 172.16.0.0/12 host address to every satellite.
 *
 * Addresses follow canonical external satellite-ID order, not ns-3 node order.
 * A dedicated legacy-compatible service interface keeps subsequent ISL
 * interface numbering compatible with the ns-3.33 platform.
 */
class SatelliteIpv4ServiceMap
{
  public:
    explicit SatelliteIpv4ServiceMap(const SatelliteIdMap& idMap);

    Ipv4Address GetServiceAddress(uint32_t satelliteId) const;
    uint32_t GetServiceInterface(uint32_t satelliteId) const;

  private:
    std::map<uint32_t, Ipv4Address> m_serviceAddresses;
    std::map<uint32_t, uint32_t> m_serviceInterfaces;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_IPV4_ADDRESSING_H
