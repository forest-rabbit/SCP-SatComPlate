/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_ENGINE_H
#define SATCOMPUTE_NETWORK_TRANSFER_ENGINE_H

#include "../routing/algorithm/capacity-aware-hrw-policy.h"
#include "../routing/algorithm/path-policy.h"
#include "../routing/state/capacity-reservation-state.h"
#include "../topology/satellite-runtime-view.h"
#include "network-transfer-application.h"
#include "network-transfer-config.h"
#include "network-transfer-receiver.h"
#include "network-transfer-records.h"

#include "ns3/callback.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ns3
{

/** Own sender/receiver applications and deterministic transfer lifecycle. */
class NetworkTransferEngine : public Object
{
  public:
    static TypeId GetTypeId();

    NetworkTransferEngine();
    ~NetworkTransferEngine() override;

    void Configure(SatelliteRuntimeView& topology,
                   const std::string& chunkMode,
                   uint32_t fixedPayloadBytes,
                   uint16_t islMtuBytes,
                   uint32_t receiverRcvBufBytes,
                   bool collectUdpSocketDrops,
                   int64_t simulationDurationNs);
    void RegisterPlans(std::vector<NetworkTransfer> plans);
    void ScheduleDeclaredTransfers();
    void StartTransferNow(uint64_t transferId,
                          Callback<void, uint64_t, int64_t> completionCallback = {});

    bool IsCompleted(uint64_t transferId) const;
    bool AreAllTransfersCompleted() const;
    const std::vector<NetworkTransfer>& GetPlans() const;

    ApplicationMetrics CollectApplicationMetrics() const;
    std::vector<TransferFlowMetadata> CollectFlowMetadata() const;
    std::vector<TransferSummaryRecord> CollectSummaries() const;
    std::vector<UdpSocketDropEvent> CollectUdpSocketDropEvents() const;
    CapacityAwareRuntimeSummary CollectCapacityAwareSummary() const;

  private:
    enum TransferState
    {
        TRANSFER_REGISTERED,
        TRANSFER_STARTED,
        TRANSFER_COMPLETED
    };

    uint32_t GetPlanIndex(uint64_t transferId) const;
    EcmpFlowKey GetFlowKey(uint32_t index) const;
    const char* GetTransferStateName(uint32_t index) const;
    void StartDeclaredTransfer(uint64_t transferId);
    void ActivateTransfer(uint64_t transferId);
    bool TryActivateCapacityAwareTransfer(uint64_t transferId);
    void TryActivatePendingCapacityAwareTransfers();
    void HandleTopologyRouteUpdate();
    void HandleSenderComplete(uint64_t transferId, int64_t sendTimeNs);
    void HandleTransferComplete(uint64_t transferId, int64_t completionTimeNs);

    SatelliteRuntimeView* m_topology{};
    std::string m_chunkMode;
    uint32_t m_fixedPayloadBytes{};
    uint16_t m_islMtuBytes{};
    uint32_t m_receiverRcvBufBytes{};
    bool m_collectUdpSocketDrops{};
    int64_t m_simulationDurationNs{};
    bool m_configured{};
    bool m_registered{};
    bool m_declaredTransfersScheduled{};
    bool m_capacityAwareRouting{};
    Ptr<FlowRouteRegistry> m_flowRouteRegistry;
    std::unique_ptr<PathPolicy> m_capacityPathPolicy;
    std::unique_ptr<CapacityReservationState> m_capacityReservationState;
    std::vector<NetworkTransfer> m_plans;
    std::vector<Ptr<NetworkTransferApplication>> m_senders;
    std::vector<Ptr<NetworkTransferReceiver>> m_receivers;
    std::vector<Ptr<NetworkTransferReceiver>> m_transferReceivers;
    std::vector<TransferState> m_states;
    std::vector<Callback<void, uint64_t, int64_t>> m_completionCallbacks;
    std::vector<uint64_t> m_pendingCapacityTransfers;
    std::map<uint64_t, uint32_t> m_planIndexes;
};

} // namespace ns3

#endif
