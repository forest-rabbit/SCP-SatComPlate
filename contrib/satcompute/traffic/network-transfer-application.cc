/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Pace each packet from the currently selected first hop or admitted path.

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

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(NetworkTransferApplication);

TypeId
NetworkTransferApplication::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::NetworkTransferApplication")
                               .SetParent<Application>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<NetworkTransferApplication>();
    return typeId;
}

NetworkTransferApplication::NetworkTransferApplication() = default;

NetworkTransferApplication::~NetworkTransferApplication() = default;

void
NetworkTransferApplication::Configure(const NetworkTransfer& transfer)
{
    NS_ABORT_MSG_IF(m_transfer.transferId != 0 || m_isRunning || m_hasStarted,
                    "NetworkTransferApplication can only be configured once");
    NS_ABORT_MSG_IF(transfer.transferId == 0,
                    "NetworkTransferApplication requires a valid transfer ID");
    m_transfer = transfer;
}

void
NetworkTransferApplication::SetSendCompleteCallback(
    Callback<void, uint64_t, int64_t> callback)
{
    NS_ABORT_MSG_IF(callback.IsNull(), "sender completion callback cannot be null");
    NS_ABORT_MSG_IF(!m_sendCompleteCallback.IsNull() || m_hasStarted,
                    "sender completion callback can only be set once");
    m_sendCompleteCallback = callback;
}

void
NetworkTransferApplication::SetPacingRateBps(uint64_t pacingRateBps)
{
    NS_ABORT_MSG_IF(pacingRateBps == 0, "transfer pacing rate must be positive");
    NS_ABORT_MSG_IF(m_hasStarted || m_pacingRateBps != 0,
                    "transfer pacing rate can only be set before sending");
    m_pacingRateBps = pacingRateBps;
}

void
NetworkTransferApplication::StartTransferNow()
{
    NS_ABORT_MSG_IF(!m_isRunning, "transfer application has not started");
    NS_ABORT_MSG_IF(m_hasStarted || m_isTerminal,
                    "terminal or started transfer application cannot start");
    m_hasStarted = true;
    m_transfer.arrivalTimeNs = Simulator::Now().GetNanoSeconds();
    m_remainingBytes = m_transfer.sizeBytes;
    m_sentPacketCount = 0;
    m_sentBytes = 0;
    m_lastSendTimeNs = -1;

    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    const int bindResult = m_socket->Bind(
        InetSocketAddress(m_transfer.sourceAddress, m_transfer.sourcePort));
    NS_ABORT_MSG_IF(bindResult != 0,
                    "transfer sender could not bind its stable source five-tuple");
    const int connectResult = m_socket->Connect(
        InetSocketAddress(m_transfer.destinationAddress, m_transfer.destinationPort));
    NS_ABORT_MSG_IF(connectResult != 0, "transfer sender could not connect its UDP socket");
    SendNextPacket();
}

void
NetworkTransferApplication::PauseForRouteUpdate()
{
    NS_ABORT_MSG_IF(!m_isRunning || !m_hasStarted || m_hasFinishedSending ||
                        m_isPausedForRouteUpdate || m_isTerminal,
                    "transfer sender cannot pause for a route update in its current state");
    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }
    m_isPausedForRouteUpdate = true;
}

void
NetworkTransferApplication::ResumeAfterRouteUpdate(uint64_t pacingRateBps)
{
    NS_ABORT_MSG_IF(!m_isRunning || !m_hasStarted || m_hasFinishedSending ||
                        !m_isPausedForRouteUpdate || m_remainingBytes == 0 ||
                        pacingRateBps == 0 || m_isTerminal,
                    "transfer sender cannot resume after the route update");
    m_pacingRateBps = pacingRateBps;
    m_isPausedForRouteUpdate = false;
    m_sendEvent = Simulator::ScheduleNow(&NetworkTransferApplication::SendNextPacket, this);
}

bool
NetworkTransferApplication::FinalizeForTerminalState()
{
    if (m_isTerminal)
    {
        return false;
    }
    m_isTerminal = true;
    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }
    m_isPausedForRouteUpdate = false;
    m_sendCompleteCallback = {};
    if (m_socket != nullptr)
    {
        m_socket->Close();
        m_socket = nullptr;
    }
    return true;
}

uint64_t
NetworkTransferApplication::GetTransferId() const
{
    return m_transfer.transferId;
}

bool
NetworkTransferApplication::HasStarted() const
{
    return m_hasStarted;
}

bool
NetworkTransferApplication::HasFinishedSending() const
{
    return m_hasFinishedSending;
}

bool
NetworkTransferApplication::IsPausedForRouteUpdate() const
{
    return m_isPausedForRouteUpdate;
}

bool
NetworkTransferApplication::IsTerminal() const
{
    return m_isTerminal;
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
    NS_ABORT_MSG_IF(m_transfer.transferId == 0, "transfer application is not configured");
    NS_ABORT_MSG_IF(m_isRunning, "transfer application was started more than once");
    m_isRunning = true;
}

void
NetworkTransferApplication::StopApplication()
{
    m_isRunning = false;
    if (m_sendEvent.IsPending())
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
    m_sendCompleteCallback = Callback<void, uint64_t, int64_t>();
    m_socket = nullptr;
    Application::DoDispose();
}

Time
NetworkTransferApplication::GetFirstHopSerializationTime(uint32_t payloadBytes) const
{
    Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
    NS_ABORT_MSG_IF(ipv4 == nullptr, "transfer source node has no IPv4 stack");
    Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
    NS_ABORT_MSG_IF(routing == nullptr, "transfer source node has no IPv4 routing protocol");

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
    Ptr<Ipv4Route> route = routing->RouteOutput(routeProbe, ipv4Header, nullptr, socketError);
    NS_ABORT_MSG_IF(route == nullptr || socketError != Socket::ERROR_NOTERROR,
                    "transfer sender could not resolve its current first hop");
    Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(route->GetOutputDevice());
    NS_ABORT_MSG_IF(device == nullptr, "transfer first hop is not point-to-point");

    DataRateValue dataRate;
    NS_ABORT_MSG_IF(!device->GetAttributeFailSafe("DataRate", dataRate),
                    "transfer first-hop data rate is unavailable");
    NS_ABORT_MSG_IF(dataRate.Get().GetBitRate() == 0,
                    "transfer first-hop data rate must be positive");

    PppHeader pppHeader;
    const uint32_t wireBytes = routeProbe->GetSize() + ipv4Header.GetSerializedSize() +
                               pppHeader.GetSerializedSize();
    uint64_t serializationRateBps = dataRate.Get().GetBitRate();
    if (m_pacingRateBps != 0)
    {
        serializationRateBps = std::min(serializationRateBps, m_pacingRateBps);
    }
    const Time serializationTime =
        DataRate(serializationRateBps).CalculateBytesTxTime(wireBytes);
    NS_ABORT_MSG_IF(serializationTime.IsZero(), "transfer serialization time is zero");
    return serializationTime;
}

void
NetworkTransferApplication::SendNextPacket()
{
    NS_ABORT_MSG_IF(m_isTerminal, "terminal transfer attempted to send");
    NS_ABORT_MSG_IF(m_socket == nullptr, "transfer sender socket is unavailable");
    NS_ABORT_MSG_IF(m_isPausedForRouteUpdate, "paused transfer attempted to send");
    NS_ABORT_MSG_IF(m_remainingBytes == 0, "completed transfer attempted an extra send");

    const uint32_t payloadBytes = static_cast<uint32_t>(
        std::min<uint64_t>(m_transfer.payloadBytesPerPacket, m_remainingBytes));
    const int sentBytes = m_socket->Send(Create<Packet>(payloadBytes));
    NS_ABORT_MSG_IF(sentBytes != static_cast<int>(payloadBytes),
                    "UDP socket did not accept the complete transfer payload");

    m_lastSendTimeNs = Simulator::Now().GetNanoSeconds();
    m_remainingBytes -= payloadBytes;
    m_sentBytes += payloadBytes;
    ++m_sentPacketCount;
    if (m_remainingBytes == 0)
    {
        NS_ABORT_MSG_IF(m_sentBytes != m_transfer.sizeBytes ||
                            m_sentPacketCount != m_transfer.packetCount,
                        "transfer packetization invariant failed");
        NS_ABORT_MSG_IF(m_hasFinishedSending, "sender completion was recorded twice");
        m_hasFinishedSending = true;
        if (!m_sendCompleteCallback.IsNull())
        {
            m_sendCompleteCallback(m_transfer.transferId, m_lastSendTimeNs);
        }
        return;
    }

    const Time serializationTime = GetFirstHopSerializationTime(payloadBytes);
    m_sendEvent = Simulator::Schedule(serializationTime,
                                      &NetworkTransferApplication::SendNextPacket,
                                      this);
}

} // namespace ns3
