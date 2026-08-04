/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Account for exact UDP application payload by stable transfer four-tuple.

#include "network-transfer-receiver.h"

#include "ns3/abort.h"
#include "ns3/inet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <limits>
#include <tuple>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(NetworkTransferReceiver);

TypeId
NetworkTransferReceiver::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::NetworkTransferReceiver")
                               .SetParent<Application>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<NetworkTransferReceiver>();
    return typeId;
}

NetworkTransferReceiver::NetworkTransferReceiver() = default;

NetworkTransferReceiver::~NetworkTransferReceiver() = default;

bool
NetworkTransferReceiver::FourTuple::operator<(const FourTuple& other) const
{
    return std::make_tuple(sourceAddress.Get(),
                           sourcePort,
                           destinationAddress.Get(),
                           destinationPort) <
           std::make_tuple(other.sourceAddress.Get(),
                           other.sourcePort,
                           other.destinationAddress.Get(),
                           other.destinationPort);
}

void
NetworkTransferReceiver::Configure(uint32_t destinationSatelliteId,
                                   Ipv4Address destinationAddress,
                                   uint16_t destinationPort,
                                   uint32_t receiverRcvBufBytes,
                                   bool collectUdpSocketDrops)
{
    NS_ABORT_MSG_IF(m_socket != nullptr, "running transfer receiver cannot be reconfigured");
    NS_ABORT_MSG_IF(destinationAddress == Ipv4Address::GetAny() || destinationPort == 0,
                    "transfer receiver requires an explicit endpoint");
    NS_ABORT_MSG_IF(receiverRcvBufBytes == 0,
                    "transfer receiver socket buffer must be positive");
    m_destinationSatelliteId = destinationSatelliteId;
    m_destinationAddress = destinationAddress;
    m_destinationPort = destinationPort;
    m_receiverRcvBufBytes = receiverRcvBufBytes;
    m_collectUdpSocketDrops = collectUdpSocketDrops;
}

void
NetworkTransferReceiver::AddExpectedTransfer(const NetworkTransfer& transfer)
{
    NS_ABORT_MSG_IF(transfer.destinationAddress != m_destinationAddress ||
                        transfer.destinationPort != m_destinationPort,
                    "transfer receiver endpoint does not match its transfer");
    const FourTuple tuple = {transfer.sourceAddress,
                             transfer.sourcePort,
                             transfer.destinationAddress,
                             transfer.destinationPort};
    const Reception reception = {transfer.transferId, transfer.sizeBytes, 0, 0, -1, -1};
    NS_ABORT_MSG_IF(!m_receptions.emplace(tuple, reception).second,
                    "transfer receiver has a duplicate four-tuple");
    NS_ABORT_MSG_IF(!m_transferTuples.emplace(transfer.transferId, tuple).second,
                    "transfer receiver has a duplicate transfer ID");
}

void
NetworkTransferReceiver::SetCompletionCallback(Callback<void, uint64_t, int64_t> callback)
{
    NS_ABORT_MSG_IF(callback.IsNull(), "receiver completion callback cannot be null");
    NS_ABORT_MSG_IF(!m_completionCallback.IsNull(),
                    "receiver completion callback can only be set once");
    m_completionCallback = callback;
}

NetworkTransferReceiver::Reception&
NetworkTransferReceiver::GetReception(uint64_t transferId)
{
    const auto tuple = m_transferTuples.find(transferId);
    NS_ABORT_MSG_IF(tuple == m_transferTuples.end(), "receiver does not contain transfer ID");
    auto reception = m_receptions.find(tuple->second);
    NS_ABORT_MSG_IF(reception == m_receptions.end(), "receiver transfer index is inconsistent");
    return reception->second;
}

const NetworkTransferReceiver::Reception&
NetworkTransferReceiver::GetReception(uint64_t transferId) const
{
    const auto tuple = m_transferTuples.find(transferId);
    NS_ABORT_MSG_IF(tuple == m_transferTuples.end(), "receiver does not contain transfer ID");
    const auto reception = m_receptions.find(tuple->second);
    NS_ABORT_MSG_IF(reception == m_receptions.end(), "receiver transfer index is inconsistent");
    return reception->second;
}

void
NetworkTransferReceiver::MarkTransferStarted(uint64_t transferId, int64_t startTimeNs)
{
    NS_ABORT_MSG_IF(startTimeNs < 0, "transfer receiver start time cannot be negative");
    Reception& reception = GetReception(transferId);
    NS_ABORT_MSG_IF(reception.startTimeNs >= 0, "receiver transfer was started twice");
    NS_ABORT_MSG_IF(reception.receivedBytes != 0 || reception.completionTimeNs >= 0,
                    "receiver observed payload before transfer start");
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
NetworkTransferReceiver::GetTransferReceivedPacketCount(uint64_t transferId) const
{
    return GetReception(transferId).receivedPacketCount;
}

int64_t
NetworkTransferReceiver::GetTransferCompletionTimeNs(uint64_t transferId) const
{
    return GetReception(transferId).completionTimeNs;
}

const std::vector<UdpSocketDropEvent>&
NetworkTransferReceiver::GetUdpSocketDropEvents() const
{
    return m_udpSocketDropEvents;
}

void
NetworkTransferReceiver::StartApplication()
{
    NS_ABORT_MSG_IF(m_destinationPort == 0 || m_receptions.empty() ||
                        m_completionCallback.IsNull(),
                    "transfer receiver is not fully configured");
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    m_socket->SetAttribute("RcvBufSize", UintegerValue(m_receiverRcvBufBytes));
    if (m_collectUdpSocketDrops)
    {
        const bool connected = m_socket->TraceConnectWithoutContext(
            "Drop",
            MakeCallback(&NetworkTransferReceiver::HandleSocketDrop, this));
        NS_ABORT_MSG_IF(!connected, "could not connect the UDP socket Drop trace");
    }
    const int bindResult =
        m_socket->Bind(InetSocketAddress(m_destinationAddress, m_destinationPort));
    NS_ABORT_MSG_IF(bindResult != 0, "transfer receiver could not bind its UDP endpoint");
    m_socket->SetRecvCallback(MakeCallback(&NetworkTransferReceiver::HandleRead, this));
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
                        "transfer receiver obtained a non-IPv4 UDP address");
        const InetSocketAddress source = InetSocketAddress::ConvertFrom(from);
        const FourTuple tuple = {source.GetIpv4(),
                                 source.GetPort(),
                                 m_destinationAddress,
                                 m_destinationPort};
        auto reception = m_receptions.find(tuple);
        NS_ABORT_MSG_IF(reception == m_receptions.end(),
                        "transfer receiver obtained an unknown four-tuple");
        NS_ABORT_MSG_IF(reception->second.startTimeNs < 0,
                        "transfer receiver obtained payload before start");

        const uint64_t payloadBytes = packet->GetSize();
        NS_ABORT_MSG_IF(
            payloadBytes > reception->second.expectedBytes - reception->second.receivedBytes,
            "received payload exceeds the declared transfer size");
        reception->second.receivedBytes += payloadBytes;
        ++reception->second.receivedPacketCount;
        NS_ABORT_MSG_IF(m_totalReceivedBytes >
                            std::numeric_limits<uint64_t>::max() - payloadBytes,
                        "transfer receiver total bytes overflow");
        m_totalReceivedBytes += payloadBytes;
        if (reception->second.receivedBytes == reception->second.expectedBytes)
        {
            NS_ABORT_MSG_IF(reception->second.completionTimeNs >= 0,
                            "transfer completion was recorded twice");
            reception->second.completionTimeNs = Simulator::Now().GetNanoSeconds();
            NS_ABORT_MSG_IF(reception->second.completionTimeNs <
                                reception->second.startTimeNs,
                            "transfer completion precedes start");
            m_completionCallback(reception->second.transferId,
                                 reception->second.completionTimeNs);
        }
    }
}

void
NetworkTransferReceiver::HandleSocketDrop(Ptr<const Packet> packet)
{
    NS_ABORT_MSG_IF(!m_collectUdpSocketDrops,
                    "UDP socket Drop callback ran while collection was disabled");
    NS_ABORT_MSG_IF(packet == nullptr || packet->GetSize() == 0,
                    "UDP socket Drop event has no payload");
    NS_ABORT_MSG_IF(m_udpSocketDropBytes >
                        std::numeric_limits<uint64_t>::max() - packet->GetSize(),
                    "UDP socket Drop byte count overflow");
    ++m_udpSocketDropPackets;
    m_udpSocketDropBytes += packet->GetSize();
    m_udpSocketDropEvents.push_back({Simulator::Now().GetNanoSeconds(),
                                     m_destinationSatelliteId,
                                     m_destinationAddress,
                                     m_destinationPort,
                                     packet->GetSize(),
                                     m_udpSocketDropPackets,
                                     m_udpSocketDropBytes,
                                     m_receiverRcvBufBytes});
}

} // namespace ns3
