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

// 接收 NetworkTransfer UDP 数据并按传输记录接收量与完成时间。

#include "network-transfer-receiver.h"

#include "ns3/abort.h"
#include "ns3/inet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <limits>
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
    m_receiverRcvBufBytes(0),
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
                                   uint16_t destinationPort,
                                   uint32_t receiverRcvBufBytes)
{
  NS_ABORT_MSG_IF(m_socket != nullptr,
                  "运行中的 NetworkTransferReceiver 不能重新配置");
  NS_ABORT_MSG_IF(destinationAddress == Ipv4Address::GetAny()
                    || destinationPort == 0,
                  "NetworkTransferReceiver 要求明确的目的地址和端口");
  NS_ABORT_MSG_IF(receiverRcvBufBytes == 0,
                  "NetworkTransferReceiver RcvBufSize 必须大于 0");
  m_destinationAddress = destinationAddress;
  m_destinationPort = destinationPort;
  m_receiverRcvBufBytes = receiverRcvBufBytes;
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
    -1,
    -1
  };
  NS_ABORT_MSG_IF(!m_receptions.insert(std::make_pair(tuple, reception)).second,
                  "NetworkTransfer receiver 出现重复四元组，transfer_id="
                    << transfer.transferId);
  NS_ABORT_MSG_IF(
    !m_transferTuples.insert(std::make_pair(transfer.transferId, tuple)).second,
    "NetworkTransfer receiver 出现重复 transfer_id="
      << transfer.transferId);
}

void
NetworkTransferReceiver::SetCompletionCallback(
  Callback<void, uint64_t, int64_t> completionCallback)
{
  NS_ABORT_MSG_IF(completionCallback.IsNull(),
                  "NetworkTransfer receiver completion callback 不能为空");
  NS_ABORT_MSG_IF(!m_completionCallback.IsNull(),
                  "NetworkTransfer receiver completion callback 只能设置一次");
  m_completionCallback = completionCallback;
}

NetworkTransferReceiver::Reception&
NetworkTransferReceiver::GetReception(uint64_t transferId)
{
  auto tuple = m_transferTuples.find(transferId);
  NS_ABORT_MSG_IF(tuple == m_transferTuples.end(),
                  "receiver 不包含 transfer_id=" << transferId);
  auto reception = m_receptions.find(tuple->second);
  NS_ABORT_MSG_IF(reception == m_receptions.end(),
                  "receiver transfer 索引不一致，transfer_id=" << transferId);
  return reception->second;
}

const NetworkTransferReceiver::Reception&
NetworkTransferReceiver::GetReception(uint64_t transferId) const
{
  auto tuple = m_transferTuples.find(transferId);
  NS_ABORT_MSG_IF(tuple == m_transferTuples.end(),
                  "receiver 不包含 transfer_id=" << transferId);
  auto reception = m_receptions.find(tuple->second);
  NS_ABORT_MSG_IF(reception == m_receptions.end(),
                  "receiver transfer 索引不一致，transfer_id=" << transferId);
  return reception->second;
}

void
NetworkTransferReceiver::MarkTransferStarted(uint64_t transferId,
                                             int64_t startTimeNs)
{
  NS_ABORT_MSG_IF(startTimeNs < 0,
                  "NetworkTransfer start time 不能为负，transfer_id="
                    << transferId);
  Reception& reception = GetReception(transferId);
  NS_ABORT_MSG_IF(reception.startTimeNs >= 0,
                  "NetworkTransfer receiver 重复启动，transfer_id="
                    << transferId);
  NS_ABORT_MSG_IF(reception.receivedBytes != 0
                    || reception.completionTimeNs >= 0,
                  "NetworkTransfer 启动前已有接收状态，transfer_id="
                    << transferId);
  reception.startTimeNs = startTimeNs;
}

uint64_t
NetworkTransferReceiver::GetTotalReceivedBytes() const
{
  return m_totalReceivedBytes;
}

uint64_t
NetworkTransferReceiver::GetTransferReceivedBytes(uint64_t transferId) const
{
  return GetReception(transferId).receivedBytes;
}

uint64_t
NetworkTransferReceiver::GetTransferReceivedPacketCount(
  uint64_t transferId) const
{
  return GetReception(transferId).receivedPacketCount;
}

int64_t
NetworkTransferReceiver::GetTransferCompletionTimeNs(
  uint64_t transferId) const
{
  return GetReception(transferId).completionTimeNs;
}

void
NetworkTransferReceiver::StartApplication()
{
  NS_ABORT_MSG_IF(m_destinationPort == 0 || m_receptions.empty()
                    || m_completionCallback.IsNull(),
                  "NetworkTransferReceiver 尚未完整配置");
  m_socket =
    Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
  m_socket->SetAttribute("RcvBufSize",
                         UintegerValue(m_receiverRcvBufBytes));
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
      NS_ABORT_MSG_IF(reception->second.startTimeNs < 0,
                      "NetworkTransfer receiver 在启动前收到 payload，"
                      "transfer_id=" << reception->second.transferId);

      uint64_t payloadBytes = packet->GetSize();
      NS_ABORT_MSG_IF(
        payloadBytes
          > reception->second.expectedBytes - reception->second.receivedBytes,
        "NetworkTransfer receiver payload 超出声明 size_bytes，transfer_id="
          << reception->second.transferId);
      reception->second.receivedBytes += payloadBytes;
      ++reception->second.receivedPacketCount;
      NS_ABORT_MSG_IF(
        m_totalReceivedBytes
          > std::numeric_limits<uint64_t>::max() - payloadBytes,
        "NetworkTransfer receiver total bytes 溢出");
      m_totalReceivedBytes += payloadBytes;
      if (reception->second.receivedBytes == reception->second.expectedBytes)
        {
          NS_ABORT_MSG_IF(reception->second.completionTimeNs >= 0,
                          "NetworkTransfer completion 重复记录，transfer_id="
                            << reception->second.transferId);
          reception->second.completionTimeNs =
            Simulator::Now().GetNanoSeconds();
          NS_ABORT_MSG_IF(reception->second.completionTimeNs
                            < reception->second.startTimeNs,
                          "NetworkTransfer completion 早于 start，transfer_id="
                            << reception->second.transferId);
          m_completionCallback(reception->second.transferId,
                               reception->second.completionTimeNs);
        }
    }
}

} // namespace ns3
