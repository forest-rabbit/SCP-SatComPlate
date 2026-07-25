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

#include "network-transfer-receiver.h"

#include "ns3/abort.h"
#include "ns3/inet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"

#include <tuple>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(NetworkTransferReceiver);

TypeId
NetworkTransferReceiver::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::NetworkTransferReceiver")
      .SetParent<Application>()
      .SetGroupName("Applications")
      .AddConstructor<NetworkTransferReceiver>();
  return typeId;
}

NetworkTransferReceiver::NetworkTransferReceiver()
  : m_destinationPort(0),
    m_totalReceivedBytes(0)
{
}

NetworkTransferReceiver::~NetworkTransferReceiver()
{
}

bool
NetworkTransferReceiver::FourTuple::operator<(const FourTuple& other) const
{
  return std::make_tuple(sourceAddress.Get(),
                         sourcePort,
                         destinationAddress.Get(),
                         destinationPort)
         < std::make_tuple(other.sourceAddress.Get(),
                           other.sourcePort,
                           other.destinationAddress.Get(),
                           other.destinationPort);
}

void
NetworkTransferReceiver::Configure(Ipv4Address destinationAddress,
                                   uint16_t destinationPort)
{
  NS_ABORT_MSG_IF(m_socket != nullptr,
                  "运行中的 NetworkTransferReceiver 不能重新配置");
  NS_ABORT_MSG_IF(destinationAddress == Ipv4Address::GetAny()
                    || destinationPort == 0,
                  "NetworkTransferReceiver 要求明确的目的地址和端口");
  m_destinationAddress = destinationAddress;
  m_destinationPort = destinationPort;
}

void
NetworkTransferReceiver::AddExpectedTransfer(
  const NetworkTransfer& transfer)
{
  NS_ABORT_MSG_IF(transfer.destinationAddress != m_destinationAddress
                    || transfer.destinationPort != m_destinationPort,
                  "NetworkTransfer receiver 与 transfer 目的四元组不匹配，"
                  "transfer_id=" << transfer.transferId);
  FourTuple tuple = {
    transfer.sourceAddress,
    transfer.sourcePort,
    transfer.destinationAddress,
    transfer.destinationPort
  };
  Reception reception = {
    transfer.transferId,
    transfer.sizeBytes,
    0,
    0,
    transfer.arrivalTimeNs,
    -1
  };
  NS_ABORT_MSG_IF(!m_receptions.insert(std::make_pair(tuple, reception)).second,
                  "NetworkTransfer receiver 出现重复四元组，transfer_id="
                    << transfer.transferId);
}

uint64_t
NetworkTransferReceiver::GetTotalReceivedBytes() const
{
  return m_totalReceivedBytes;
}

uint64_t
NetworkTransferReceiver::GetTransferReceivedBytes(uint64_t transferId) const
{
  for (const auto& item : m_receptions)
    {
      if (item.second.transferId == transferId)
        {
          return item.second.receivedBytes;
        }
    }
  NS_FATAL_ERROR("receiver 不包含 transfer_id=" << transferId);
  return 0;
}

uint64_t
NetworkTransferReceiver::GetTransferReceivedPacketCount(
  uint64_t transferId) const
{
  for (const auto& item : m_receptions)
    {
      if (item.second.transferId == transferId)
        {
          return item.second.receivedPacketCount;
        }
    }
  NS_FATAL_ERROR("receiver 不包含 transfer_id=" << transferId);
  return 0;
}

int64_t
NetworkTransferReceiver::GetTransferCompletionTimeNs(
  uint64_t transferId) const
{
  for (const auto& item : m_receptions)
    {
      if (item.second.transferId == transferId)
        {
          return item.second.completionTimeNs;
        }
    }
  NS_FATAL_ERROR("receiver 不包含 transfer_id=" << transferId);
  return -1;
}

void
NetworkTransferReceiver::StartApplication()
{
  NS_ABORT_MSG_IF(m_destinationPort == 0 || m_receptions.empty(),
                  "NetworkTransferReceiver 尚未完整配置");
  m_socket =
    Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
  int bindResult = m_socket->Bind(
    InetSocketAddress(m_destinationAddress, m_destinationPort));
  NS_ABORT_MSG_IF(bindResult != 0,
                  "NetworkTransfer receiver bind 失败: "
                    << m_destinationAddress << ":" << m_destinationPort);
  m_socket->SetRecvCallback(
    MakeCallback(&NetworkTransferReceiver::HandleRead, this));
}

void
NetworkTransferReceiver::StopApplication()
{
  if (m_socket != nullptr)
    {
      m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
      m_socket->Close();
      m_socket = nullptr;
    }
}

void
NetworkTransferReceiver::DoDispose()
{
  m_socket = nullptr;
  Application::DoDispose();
}

void
NetworkTransferReceiver::HandleRead(Ptr<Socket> socket)
{
  Address from;
  Ptr<Packet> packet;
  while ((packet = socket->RecvFrom(from)) != nullptr)
    {
      NS_ABORT_MSG_IF(!InetSocketAddress::IsMatchingType(from),
                      "NetworkTransfer receiver 收到非 IPv4 UDP 地址");
      InetSocketAddress source = InetSocketAddress::ConvertFrom(from);
      FourTuple tuple = {
        source.GetIpv4(),
        source.GetPort(),
        m_destinationAddress,
        m_destinationPort
      };
      auto reception = m_receptions.find(tuple);
      NS_ABORT_MSG_IF(reception == m_receptions.end(),
                      "NetworkTransfer receiver 收到未知四元组: "
                        << source.GetIpv4() << ":" << source.GetPort()
                        << " -> " << m_destinationAddress << ":"
                        << m_destinationPort);

      uint64_t payloadBytes = packet->GetSize();
      NS_ABORT_MSG_IF(
        payloadBytes
          > reception->second.expectedBytes - reception->second.receivedBytes,
        "NetworkTransfer receiver payload 超出声明 size_bytes，transfer_id="
          << reception->second.transferId);
      reception->second.receivedBytes += payloadBytes;
      ++reception->second.receivedPacketCount;
      m_totalReceivedBytes += payloadBytes;
      if (reception->second.receivedBytes == reception->second.expectedBytes)
        {
          NS_ABORT_MSG_IF(reception->second.completionTimeNs >= 0,
                          "NetworkTransfer completion 重复记录，transfer_id="
                            << reception->second.transferId);
          reception->second.completionTimeNs =
            Simulator::Now().GetNanoSeconds();
          NS_ABORT_MSG_IF(reception->second.completionTimeNs
                            < reception->second.arrivalTimeNs,
                          "NetworkTransfer completion 早于 arrival，transfer_id="
                            << reception->second.transferId);
        }
    }
}

} // namespace ns3
