/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_APPLICATION_H
#define SATCOMPUTE_NETWORK_TRANSFER_APPLICATION_H

#include "network-transfer-config.h"

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

#include <cstdint>

namespace ns3
{

/** Packetize and pace one deterministic UDP network transfer. */
class NetworkTransferApplication : public Application
{
  public:
    static TypeId GetTypeId();

    NetworkTransferApplication();
    ~NetworkTransferApplication() override;

    void Configure(const NetworkTransfer& transfer);
    void SetSendCompleteCallback(Callback<void, uint64_t, int64_t> callback);
    void SetPacingRateBps(uint64_t pacingRateBps);
    void StartTransferNow();
    void PauseForRouteUpdate();
    void ResumeAfterRouteUpdate(uint64_t pacingRateBps);

    uint64_t GetTransferId() const;
    bool HasStarted() const;
    bool HasFinishedSending() const;
    bool IsPausedForRouteUpdate() const;
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
    uint64_t m_remainingBytes{};
    uint64_t m_sentPacketCount{};
    uint64_t m_sentBytes{};
    uint64_t m_pacingRateBps{};
    int64_t m_lastSendTimeNs{-1};
    bool m_isRunning{};
    bool m_hasStarted{};
    bool m_hasFinishedSending{};
    bool m_isPausedForRouteUpdate{};
    Callback<void, uint64_t, int64_t> m_sendCompleteCallback;
};

} // namespace ns3

#endif
