/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FLOW_ROUTE_REGISTRY_H
#define SATCOMPUTE_FLOW_ROUTE_REGISTRY_H

#include "../common/ecmp-flow-key.h"
#include "../common/ecmp-route-candidate.h"
#include "flow-route-state.h"
#include "size-aware-load-state.h"

#include "ns3/object.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

struct FlowRouteMetadata
{
    uint64_t transferId;
    uint64_t declaredBytes;
    bool senderActive;
};

struct FlowRouteReservationEvent
{
    int64_t simulationTimeNs;
    std::string action;
    std::string selectionReason;
    uint64_t routeEpoch;
    uint32_t nodeId;
    EcmpFlowKey flowKey;
    uint64_t transferId;
    uint64_t declaredBytes;
    EcmpRouteCandidate candidate;
    uint64_t candidateReservedBefore;
    uint64_t candidateReservedAfter;
    uint64_t totalReservedBefore;
    uint64_t totalReservedAfter;
};

class FlowRouteRegistry : public Object, public FlowRouteState
{
  public:
    static TypeId GetTypeId();

    FlowRouteRegistry();
    ~FlowRouteRegistry() override;

    void RegisterTransfer(const EcmpFlowKey& flowKey, uint64_t transferId, uint64_t declaredBytes);
    void BeginSending(const EcmpFlowKey& flowKey);
    void FinishSending(const EcmpFlowKey& flowKey);
    void FinishReceiving(const EcmpFlowKey& flowKey);

    bool IsRegistered(const EcmpFlowKey& flowKey) const;
    bool IsSenderActive(const EcmpFlowKey& flowKey) const override;
    FlowRouteMetadata GetMetadata(const EcmpFlowKey& flowKey) const;

    bool FindAssignment(uint32_t nodeId,
                        const EcmpFlowKey& flowKey,
                        FlowRouteAssignment& assignment) const override;
    void RecordAssignment(uint32_t nodeId,
                          const EcmpFlowKey& flowKey,
                          const EcmpRouteCandidate& candidate,
                          uint64_t routeEpoch,
                          const std::string& selectionReason) override;
    void ValidateAssignment(uint32_t nodeId,
                            const EcmpFlowKey& flowKey,
                            uint64_t routeEpoch,
                            const std::string& selectionReason) override;
    void ReleaseInvalidAssignment(uint32_t nodeId,
                                  const EcmpFlowKey& flowKey,
                                  uint64_t routeEpoch) override;
    void ReleaseAssignmentsForRouteUpdate(const EcmpFlowKey& flowKey, uint64_t routeEpoch);

    const SizeAwareLoadState& GetSizeAwareLoadState() const;
    uint64_t GetReservedBytes(uint32_t nodeId, const EcmpRouteCandidate& candidate) const;
    uint64_t GetTotalReservedBytes() const;
    uint64_t GetPeakReservedBytes() const;
    uint64_t GetPeakCandidateReservedBytes() const;
    uint32_t GetRegisteredFlowCount() const;
    uint32_t GetActiveFlowCount() const;
    uint32_t GetAssignmentCount() const;
    const std::vector<FlowRouteReservationEvent>& GetEvents() const;
    void Clear();

  private:
    struct NodeFlowKey
    {
        uint32_t nodeId;
        EcmpFlowKey flowKey;

        bool operator<(const NodeFlowKey& other) const;
    };

    void ReleaseAssignment(std::map<NodeFlowKey, FlowRouteAssignment>::iterator assignment,
                           const std::string& action,
                           uint64_t routeEpoch);
    void FinishFlow(const EcmpFlowKey& flowKey, const std::string& releaseAction);
    void RecordEvent(const std::string& action,
                     const std::string& selectionReason,
                     uint64_t routeEpoch,
                     uint32_t nodeId,
                     const EcmpFlowKey& flowKey,
                     const EcmpRouteCandidate& candidate,
                     uint64_t candidateReservedBefore,
                     uint64_t candidateReservedAfter,
                     uint64_t totalReservedBefore,
                     uint64_t totalReservedAfter);

    std::map<EcmpFlowKey, FlowRouteMetadata> m_flows;
    std::map<uint64_t, EcmpFlowKey> m_flowKeysByTransferId;
    std::map<NodeFlowKey, FlowRouteAssignment> m_assignments;
    SizeAwareLoadState m_sizeAwareLoadState;
    std::vector<FlowRouteReservationEvent> m_events;
    uint32_t m_activeFlowCount;
};

} // namespace ns3

#endif
