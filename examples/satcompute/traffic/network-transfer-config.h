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

#ifndef SATCOMPUTE_NETWORK_TRANSFER_CONFIG_H
#define SATCOMPUTE_NETWORK_TRANSFER_CONFIG_H

#include "../topo.h"

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

static const uint16_t NETWORK_TRANSFER_DESTINATION_PORT = 9000;
static const uint16_t NETWORK_TRANSFER_FIRST_SOURCE_PORT = 10000;

struct NetworkTransfer
{
  uint64_t transferId;
  uint32_t sourceSatelliteId;
  uint32_t destinationSatelliteId;
  uint64_t sizeBytes;
  int64_t arrivalTimeNs;
  Ipv4Address sourceAddress;
  Ipv4Address destinationAddress;
  uint16_t sourcePort;
  uint16_t destinationPort;
  uint32_t payloadBytesPerPacket;
  uint64_t derivedPacketIntervalNs;
  uint64_t packetCount;
  uint32_t finalPacketPayloadBytes;
  int64_t lastScheduledSendTimeNs;

  NetworkTransfer();
};

uint64_t DeriveNetworkTransferPacketIntervalNs(uint32_t payloadBytes,
                                               uint64_t sendRateBps);

std::vector<NetworkTransfer> ReadNetworkTransferTrace(
  const std::string& filename,
  uint32_t payloadBytes,
  uint64_t packetIntervalNs,
  double simulationDurationSeconds,
  const SatelliteTopology& topology);

} // namespace ns3

#endif
