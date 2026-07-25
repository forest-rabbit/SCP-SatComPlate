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

#include "network-transfer-application.h"

#include "ns3/abort.h"
#include "ns3/inet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"

#include <algorithm>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(NetworkTransferApplication);

TypeId
NetworkTransferApplication::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::NetworkTransferApplication")
      .SetParent<Application>()
      .SetGroupName("Applications")
      .AddConstructor<NetworkTransferApplication>();
  return typeId;
}

NetworkTransferApplication::NetworkTransferApplication()
  : m_remainingBytes(0),
    m_sentPacketCount(0),
    m_sentBytes(0)
{
}

NetworkTransferApplication::~NetworkTransferApplication()
{
}

void
NetworkTransferApplication::Configure(const NetworkTransfer& transfer)
{
  NS_ABORT_MSG_IF(m_socket != nullptr,
                  "运行中的 NetworkTransferApplication 不能重新配置");
  NS_ABORT_MSG_IF(transfer.transferId == 0,
                  "NetworkTransferApplication 要求有效 transfer_id");
  m_transfer = transfer;
}

uint64_t
NetworkTransferApplication::GetTransferId() const
{
  return m_transfer.transferId;
}

uint64_t
NetworkTransferApplication::GetSentPacketCount() const
{
  return m_sentPacketCount;
}

uint64_t
NetworkTransferApplication::GetSentBytes() const
{
  return m_sentBytes;
}

void
NetworkTransferApplication::StartApplication()
{
  NS_ABORT_MSG_IF(m_transfer.transferId == 0,
                  "NetworkTransferApplication 尚未配置");
  m_remainingBytes = m_transfer.sizeBytes;
  m_sentPacketCount = 0;
  m_sentBytes = 0;

  m_socket =
    Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
  int bindResult = m_socket->Bind(
    InetSocketAddress(m_transfer.sourceAddress, m_transfer.sourcePort));
  NS_ABORT_MSG_IF(bindResult != 0,
                  "NetworkTransfer UDP source bind 失败，transfer_id="
                    << m_transfer.transferId << "，source="
                    << m_transfer.sourceAddress << ":"
                    << m_transfer.sourcePort);
  int connectResult = m_socket->Connect(
    InetSocketAddress(m_transfer.destinationAddress,
                      m_transfer.destinationPort));
  NS_ABORT_MSG_IF(connectResult != 0,
                  "NetworkTransfer UDP connect 失败，transfer_id="
                    << m_transfer.transferId);
  SendNextPacket();
}

void
NetworkTransferApplication::StopApplication()
{
  if (m_sendEvent.IsRunning())
    {
      Simulator::Cancel(m_sendEvent);
    }
  if (m_socket != nullptr)
    {
      m_socket->Close();
      m_socket = nullptr;
    }
}

void
NetworkTransferApplication::DoDispose()
{
  m_socket = nullptr;
  Application::DoDispose();
}

void
NetworkTransferApplication::SendNextPacket()
{
  NS_ABORT_MSG_IF(m_socket == nullptr,
                  "NetworkTransfer sender socket 不可用，transfer_id="
                    << m_transfer.transferId);
  NS_ABORT_MSG_IF(m_remainingBytes == 0,
                  "NetworkTransfer sender 出现额外发送事件，transfer_id="
                    << m_transfer.transferId);

  uint32_t payloadBytes = static_cast<uint32_t>(
    std::min<uint64_t>(NETWORK_TRANSFER_PAYLOAD_BYTES, m_remainingBytes));
  int sentBytes = m_socket->Send(Create<Packet>(payloadBytes));
  NS_ABORT_MSG_IF(sentBytes != static_cast<int>(payloadBytes),
                  "NetworkTransfer UDP payload 发送失败，transfer_id="
                    << m_transfer.transferId << "，expected="
                    << payloadBytes << "，actual=" << sentBytes);

  m_remainingBytes -= payloadBytes;
  m_sentBytes += payloadBytes;
  ++m_sentPacketCount;
  if (m_remainingBytes == 0)
    {
      NS_ABORT_MSG_IF(m_sentBytes != m_transfer.sizeBytes
                        || m_sentPacketCount != m_transfer.packetCount,
                      "NetworkTransfer packetization invariant 失败，transfer_id="
                        << m_transfer.transferId);
      return;
    }

  m_sendEvent = Simulator::Schedule(
    NanoSeconds(static_cast<int64_t>(m_transfer.packetIntervalNs)),
    &NetworkTransferApplication::SendNextPacket,
    this);
}

} // namespace ns3
