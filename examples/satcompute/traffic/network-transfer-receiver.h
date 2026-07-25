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

#ifndef SATCOMPUTE_NETWORK_TRANSFER_RECEIVER_H
#define SATCOMPUTE_NETWORK_TRANSFER_RECEIVER_H

#include "network-transfer-config.h"

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

#include <cstdint>
#include <map>

namespace ns3 {

class NetworkTransferReceiver : public Application
{
public:
  static TypeId GetTypeId();

  NetworkTransferReceiver();
  ~NetworkTransferReceiver() override;

  void Configure(Ipv4Address destinationAddress, uint16_t destinationPort);
  void AddExpectedTransfer(const NetworkTransfer& transfer);

  uint64_t GetTotalReceivedBytes() const;
  uint64_t GetTransferReceivedBytes(uint64_t transferId) const;
  uint64_t GetTransferReceivedPacketCount(uint64_t transferId) const;

private:
  struct FourTuple
  {
    Ipv4Address sourceAddress;
    uint16_t sourcePort;
    Ipv4Address destinationAddress;
    uint16_t destinationPort;

    bool operator<(const FourTuple& other) const;
  };

  struct Reception
  {
    uint64_t transferId;
    uint64_t expectedBytes;
    uint64_t receivedBytes;
    uint64_t receivedPacketCount;
  };

  void StartApplication() override;
  void StopApplication() override;
  void DoDispose() override;
  void HandleRead(Ptr<Socket> socket);

  Ipv4Address m_destinationAddress;
  uint16_t m_destinationPort;
  Ptr<Socket> m_socket;
  std::map<FourTuple, Reception> m_receptions;
  uint64_t m_totalReceivedBytes;
};

} // namespace ns3

#endif
