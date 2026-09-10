/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_ENGINE_H
#define SATCOMPUTE_NETWORK_TRANSFER_ENGINE_H

#include "../routing/algorithm/path-policy.h"
#include "../routing/state/capacity-reservation-state.h"
#include "../topology/satellite-runtime-view.h"
#include "network-transfer-application.h"
#include "network-transfer-config.h"
#include "network-transfer-receiver.h"
#include "network-transfer-records.h"

#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ns3
{

/** Read-only new-flow admission estimate. Never reserves capacity or changes an in-flight path. */
struct AdmissiblePathEstimate
{
    bool reachable{}, admissible{}, local{};
    CapacityAwarePath path;
    int64_t propagationNs{};
    std::string failureReason;
    std::optional<int64_t> TransferTimeNs(uint64_t bytes) const;
};

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
    /** Append one positive-byte runtime flow; ordinary plan IDs/ports remain unchanged. */
    void RegisterRuntimePlan(NetworkTransfer plan, bool businessResult = false);
    bool IsRuntimeTransfer(uint64_t transferId) const;
    /** Runtime recovery RESULT is business traffic, unlike backup/input replay traffic. */
    bool IsProtectionTransfer(uint64_t transferId) const;
    /** Current directed-link residual capacity for causal recovery estimates; reserves nothing. */
    uint64_t GetResidualRateBps(uint32_t source, const EcmpRouteCandidate& route) const;
    /** Preview the next runtime flow's current policy; actual registration rechecks admission. */
    AdmissiblePathEstimate EstimateAdmissiblePath(uint32_t source, uint32_t destination) const;
    void SetTerminalObserver(uint64_t transferId, Callback<void, uint64_t, int64_t> observer);
    uint64_t GetReceivedBytes(uint64_t transferId) const;
    void StartTransferNow(uint64_t transferId,
                          Callback<void, uint64_t, int64_t> completionCallback = {});
    bool FinalizeTransferIfActive(uint64_t transferId,
                                  TransferTerminalState state,
                                  TransferTerminalReason reason);
    /** Finalize a group before admitting any newly unblocked flows; returns terminalized count. */
    uint64_t FinalizeTransfersIfActive(const std::vector<uint64_t>& transferIds,
                                       TransferTerminalState state,
                                       TransferTerminalReason reason);

    bool IsCompleted(uint64_t transferId) const;
    bool IsTerminal(uint64_t transferId) const;
    TransferRuntimeState GetTransferState(uint64_t transferId) const;
    std::optional<TransferTerminalReason> GetTerminalReason(uint64_t transferId) const;
    int64_t GetTerminalTimeNs(uint64_t transferId) const;
    uint64_t GetStalePacketCount(uint64_t transferId) const;
    int64_t GetCapacityWaitingTimeNs(uint64_t transferId) const;
    bool AreAllTransfersCompleted(bool includeRuntime = true) const;
    const std::vector<NetworkTransfer>& GetPlans() const;

    ApplicationMetrics CollectApplicationMetrics() const;
    std::vector<TransferFlowMetadata> CollectFlowMetadata() const;
    std::vector<TransferSummaryRecord> CollectSummaries() const;
    std::vector<UdpSocketDropEvent> CollectUdpSocketDropEvents() const;
    CapacityAwareRuntimeSummary CollectCapacityAwareSummary() const;
    /** Install an optional read-only per-directed-link reservation observer.
     * @param observer Source satellite/interface/reserved bit/s callback, or empty.
     */
    void SetCapacityReservationObserver(Callback<void, uint32_t, uint32_t, uint64_t> observer);

  private:
    uint32_t GetPlanIndex(uint64_t transferId) const;
    EcmpFlowKey GetFlowKey(uint32_t index) const;
    void ActivateTransfer(uint64_t transferId);
    void RuntimeApplicationsReady(uint64_t transferId);
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
    bool m_capacityAwareRouting{};
    uint32_t m_finalizationBatchDepth{};
    Ptr<FlowRouteRegistry> m_flowRouteRegistry;
    std::unique_ptr<PathPolicy> m_capacityPathPolicy;
    std::unique_ptr<CapacityReservationState> m_capacityReservationState;
    std::vector<NetworkTransfer> m_plans;
    std::vector<Ptr<NetworkTransferApplication>> m_senders;
    std::vector<Ptr<NetworkTransferReceiver>> m_receivers;
    std::vector<Ptr<NetworkTransferReceiver>> m_transferReceivers;
    std::vector<TransferRuntimeState> m_states;
    std::vector<std::optional<TransferTerminalReason>> m_terminalReasons;
    std::vector<int64_t> m_terminalTimesNs;
    std::vector<int64_t> m_capacityWaitStartTimesNs;
    std::vector<int64_t> m_capacityWaitingTimesNs;
    std::vector<EventId> m_activationEvents;
    std::vector<Callback<void, uint64_t, int64_t>> m_completionCallbacks;
    std::vector<uint64_t> m_pendingCapacityTransfers;
    std::map<uint64_t, uint32_t> m_planIndexes;
    std::map<uint32_t, uint32_t> m_nextSourceOrdinal;
    std::map<uint32_t, Ptr<NetworkTransferReceiver>> m_receiversBySatellite;
    std::set<uint64_t> m_runtimeTransfers;
    std::set<uint64_t> m_businessResults;
    std::set<uint64_t> m_runtimeStarting;
    std::map<uint64_t, Callback<void, uint64_t, int64_t>> m_terminalObservers;
};

} // namespace ns3

#endif
