/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_RECEIVER_H
#define SATCOMPUTE_NETWORK_TRANSFER_RECEIVER_H

#include "network-transfer-config.h"

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

#include <cstdint>
#include <map>
#include <vector>

namespace ns3
{

struct UdpSocketDropEvent
{
    int64_t simulationTimeNs;
    uint32_t destinationSatelliteId;
    Ipv4Address destinationAddress;
    uint16_t destinationPort;
    uint32_t packetSizeBytes;
    uint64_t cumulativeDropPackets;
    uint64_t cumulativeDropBytes;
    uint32_t receiverRcvBufBytes;
};

/** Receive UDP payload and identify transfers from their stable four-tuples. */
class NetworkTransferReceiver : public Application
{
  public:
    static TypeId GetTypeId();

    NetworkTransferReceiver();
    ~NetworkTransferReceiver() override;

    void Configure(uint32_t destinationSatelliteId,
                   Ipv4Address destinationAddress,
                   uint16_t destinationPort,
                   uint32_t receiverRcvBufBytes,
                   bool collectUdpSocketDrops);
    void AddExpectedTransfer(const NetworkTransfer& transfer);
    void SetCompletionCallback(Callback<void, uint64_t, int64_t> callback);
    void MarkTransferStarted(uint64_t transferId, int64_t startTimeNs);

    uint64_t GetTotalReceivedBytes() const;
    uint64_t GetTransferReceivedBytes(uint64_t transferId) const;
    uint64_t GetTransferReceivedPacketCount(uint64_t transferId) const;
    int64_t GetTransferCompletionTimeNs(uint64_t transferId) const;
    const std::vector<UdpSocketDropEvent>& GetUdpSocketDropEvents() const;

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
        int64_t startTimeNs;
        int64_t completionTimeNs;
    };

    Reception& GetReception(uint64_t transferId);
    const Reception& GetReception(uint64_t transferId) const;
    void StartApplication() override;
    void StopApplication() override;
    void DoDispose() override;
    void HandleRead(Ptr<Socket> socket);
    void HandleSocketDrop(Ptr<const Packet> packet);

    uint32_t m_destinationSatelliteId{};
    Ipv4Address m_destinationAddress;
    uint16_t m_destinationPort{};
    uint32_t m_receiverRcvBufBytes{};
    bool m_collectUdpSocketDrops{};
    Ptr<Socket> m_socket;
    std::map<FourTuple, Reception> m_receptions;
    std::map<uint64_t, FourTuple> m_transferTuples;
    uint64_t m_totalReceivedBytes{};
    uint64_t m_udpSocketDropPackets{};
    uint64_t m_udpSocketDropBytes{};
    std::vector<UdpSocketDropEvent> m_udpSocketDropEvents;
    Callback<void, uint64_t, int64_t> m_completionCallback;
};

} // namespace ns3

#endif
