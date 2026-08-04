/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "satellite-ipv4-addressing.h"

#include "ns3/csma-net-device.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4.h"
#include "ns3/mac48-address.h"
#include "ns3/packet.h"

#include <string>

namespace ns3
{

SatelliteIpv4ServiceMap::SatelliteIpv4ServiceMap(const SatelliteIdMap& idMap)
{
    constexpr uint32_t firstServiceAddress = 0xac100001u;
    constexpr uint32_t serviceAddressCount = 1u << 20;
    if (idMap.GetNodeCount() > serviceAddressCount - 1)
    {
        throw SatelliteIpv4AddressingError(
            "172.16.0.0/12 does not contain enough satellite service addresses");
    }

    for (const uint32_t satelliteId : idMap.GetCanonicalSatelliteIds())
    {
        if (idMap.GetNodeBySatelliteId(satelliteId)->GetObject<Ipv4>() == nullptr)
        {
            throw SatelliteIpv4AddressingError(
                "satellite " + std::to_string(satelliteId) +
                " has no IPv4 stack before service-address assignment");
        }
    }

    uint32_t canonicalIndex = 0;
    for (const uint32_t satelliteId : idMap.GetCanonicalSatelliteIds())
    {
        const Ptr<Node> node = idMap.GetNodeBySatelliteId(satelliteId);
        const Ptr<CsmaNetDevice> device = CreateObject<CsmaNetDevice>();
        device->SetAddress(Mac48Address::Allocate());
        device->SetQueue(CreateObject<DropTailQueue<Packet>>());
        node->AddDevice(device);

        const Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        const uint32_t interface = ipv4->AddInterface(device);
        const Ipv4Address address(firstServiceAddress + canonicalIndex);
        if (!ipv4->AddAddress(
                interface,
                Ipv4InterfaceAddress(address, Ipv4Mask("255.255.255.255"))))
        {
            throw SatelliteIpv4AddressingError(
                "cannot add IPv4 service address for satellite " +
                std::to_string(satelliteId));
        }
        ipv4->SetMetric(interface, 1);
        ipv4->SetUp(interface);
        m_serviceAddresses.emplace(satelliteId, address);
        m_serviceInterfaces.emplace(satelliteId, interface);
        ++canonicalIndex;
    }
}

Ipv4Address
SatelliteIpv4ServiceMap::GetServiceAddress(uint32_t satelliteId) const
{
    const auto address = m_serviceAddresses.find(satelliteId);
    if (address == m_serviceAddresses.end())
    {
        throw SatelliteIpv4AddressingError("unknown external satellite ID " +
                                           std::to_string(satelliteId));
    }
    return address->second;
}

uint32_t
SatelliteIpv4ServiceMap::GetServiceInterface(uint32_t satelliteId) const
{
    const auto interface = m_serviceInterfaces.find(satelliteId);
    if (interface == m_serviceInterfaces.end())
    {
        throw SatelliteIpv4AddressingError("unknown external satellite ID " +
                                           std::to_string(satelliteId));
    }
    return interface->second;
}

} // namespace ns3
