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
#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ppp-header.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"
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
    m_sentBytes(0),
    m_lastSendTimeNs(-1)
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

int64_t
NetworkTransferApplication::GetLastSendTimeNs() const
{
  return m_lastSendTimeNs;
}

void
NetworkTransferApplication::StartApplication()
{
  NS_ABORT_MSG_IF(m_transfer.transferId == 0,
                  "NetworkTransferApplication 尚未配置");
  m_remainingBytes = m_transfer.sizeBytes;
  m_sentPacketCount = 0;
  m_sentBytes = 0;
  m_lastSendTimeNs = -1;

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

Time
NetworkTransferApplication::GetFirstHopSerializationTime(
  uint32_t payloadBytes) const
{
  Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
  NS_ABORT_MSG_IF(ipv4 == nullptr,
                  "NetworkTransfer source node 缺少 IPv4，transfer_id="
                    << m_transfer.transferId);
  Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
  NS_ABORT_MSG_IF(routing == nullptr,
                  "NetworkTransfer source node 缺少 IPv4 routing，transfer_id="
                    << m_transfer.transferId);

  Ptr<Packet> routeProbe = Create<Packet>(payloadBytes);
  UdpHeader udpHeader;
  udpHeader.SetSourcePort(m_transfer.sourcePort);
  udpHeader.SetDestinationPort(m_transfer.destinationPort);
  routeProbe->AddHeader(udpHeader);

  Ipv4Header ipv4Header;
  ipv4Header.SetSource(m_transfer.sourceAddress);
  ipv4Header.SetDestination(m_transfer.destinationAddress);
  ipv4Header.SetProtocol(17);
  ipv4Header.SetPayloadSize(routeProbe->GetSize());

  Socket::SocketErrno socketError = Socket::ERROR_NOTERROR;
  Ptr<Ipv4Route> route =
    routing->RouteOutput(routeProbe, ipv4Header, nullptr, socketError);
  NS_ABORT_MSG_IF(route == nullptr || socketError != Socket::ERROR_NOTERROR,
                  "NetworkTransfer 无法查询当前首跳路由，transfer_id="
                    << m_transfer.transferId);
  Ptr<PointToPointNetDevice> device =
    DynamicCast<PointToPointNetDevice>(route->GetOutputDevice());
  NS_ABORT_MSG_IF(device == nullptr,
                  "NetworkTransfer 首跳不是 PointToPointNetDevice，transfer_id="
                    << m_transfer.transferId);

  DataRateValue dataRate;
  NS_ABORT_MSG_IF(!device->GetAttributeFailSafe("DataRate", dataRate),
                  "NetworkTransfer 无法读取首跳 DataRate，transfer_id="
                    << m_transfer.transferId);
  NS_ABORT_MSG_IF(dataRate.Get().GetBitRate() == 0,
                  "NetworkTransfer 首跳 DataRate 为零，transfer_id="
                    << m_transfer.transferId);

  PppHeader pppHeader;
  uint32_t wireBytes =
    routeProbe->GetSize()
    + ipv4Header.GetSerializedSize()
    + pppHeader.GetSerializedSize();
  Time serializationTime =
    dataRate.Get().CalculateBytesTxTime(wireBytes);
  NS_ABORT_MSG_IF(serializationTime.IsZero(),
                  "NetworkTransfer 首跳序列化时间为零，transfer_id="
                    << m_transfer.transferId);
  return serializationTime;
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
    std::min<uint64_t>(m_transfer.payloadBytesPerPacket, m_remainingBytes));
  int sentBytes = m_socket->Send(Create<Packet>(payloadBytes));
  NS_ABORT_MSG_IF(sentBytes != static_cast<int>(payloadBytes),
                  "NetworkTransfer UDP payload 发送失败，transfer_id="
                    << m_transfer.transferId << "，expected="
                    << payloadBytes << "，actual=" << sentBytes);

  m_lastSendTimeNs = Simulator::Now().GetNanoSeconds();
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

  Time serializationTime =
    GetFirstHopSerializationTime(payloadBytes);
  m_sendEvent = Simulator::Schedule(
    serializationTime,
    &NetworkTransferApplication::SendNextPacket,
    this);
}

} // namespace ns3
