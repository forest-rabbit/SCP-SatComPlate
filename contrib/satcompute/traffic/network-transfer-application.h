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

#ifndef SATCOMPUTE_NETWORK_TRANSFER_APPLICATION_H
#define SATCOMPUTE_NETWORK_TRANSFER_APPLICATION_H

#include "network-transfer-config.h"

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

#include <cstdint>

namespace ns3 {

class NetworkTransferApplication : public Application
{
public:
  static TypeId GetTypeId();

  NetworkTransferApplication();
  ~NetworkTransferApplication() override;

  void Configure(const NetworkTransfer& transfer);
  void StartTransferNow();

  uint64_t GetTransferId() const;
  bool HasStarted() const;
  uint64_t GetSentPacketCount() const;
  uint64_t GetSentBytes() const;
  int64_t GetLastSendTimeNs() const;

private:
  void StartApplication() override;
  void StopApplication() override;
  void DoDispose() override;
  void SendNextPacket();
  Time GetFirstHopSerializationTime(uint32_t payloadBytes) const;

  NetworkTransfer m_transfer;
  Ptr<Socket> m_socket;
  EventId m_sendEvent;
  uint64_t m_remainingBytes;
  uint64_t m_sentPacketCount;
  uint64_t m_sentBytes;
  int64_t m_lastSendTimeNs;
  bool m_isRunning;
  bool m_hasStarted;
};

} // namespace ns3

#endif
