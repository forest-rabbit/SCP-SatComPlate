/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_IPV4_GLOBAL_ROUTING_H
#define SATCOMPUTE_IPV4_GLOBAL_ROUTING_H

#include "../algorithm/hash-per-flow-policy.h"
#include "../algorithm/hrw-per-flow-policy.h"
#include "../common/ecmp-flow-key.h"
#include "../common/ecmp-route-candidate.h"
#include "../common/routing-mode.h"
#include "../state/flow-route-registry.h"
#include "ns3/global-routing.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ns3
{

struct EcmpRouteDecisionEvent
{
    int64_t simulationTimeNs;
    uint64_t routeEpoch;
    uint32_t nodeId;
    EcmpFlowKey flowKey;
    bool hasFiveTuple;
    uint32_t candidateCountBeforeDedup;
    uint32_t candidateCountAfterDedup;
    int64_t selectedIndex;
    Ipv4Address selectedGateway;
    int64_t selectedOutputInterface;
    uint64_t hashValue;
    std::string selectionReason;
};

class SatComputeIpv4GlobalRouting : public Ipv4GlobalRouting
{
  public:
    static TypeId GetTypeId();

    SatComputeIpv4GlobalRouting();
    ~SatComputeIpv4GlobalRouting() override;

    void Configure(RoutingMode selectionMode,
                   uint64_t hashSeed,
                   Ptr<FlowRouteRegistry> flowRouteRegistry = nullptr);
    void SetSatelliteId(uint32_t satelliteId);
    void AdvanceRouteEpoch();
    void InvalidateDecisionCache(const EcmpFlowKey& flowKey);
    uint64_t GetRouteEpoch() const;
    /** Preview a NEW flow with isolated assignment state and the current shared load view. */
    uint32_t PreviewNextHop(const EcmpFlowKey& key,
                            const std::vector<EcmpRouteCandidate>& candidates) const;
    std::vector<EcmpRouteCandidate> GetEffectiveRouteCandidates(Ipv4Address destination);

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> packet,
                               const Ipv4Header& header,
                               Ptr<NetDevice> outputInterface,
                               Socket::SocketErrno& socketError) override;
    bool RouteInput(Ptr<const Packet> packet,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> inputDevice,
                    const UnicastForwardCallback& unicastCallback,
                    const MulticastForwardCallback& multicastCallback,
                    const LocalDeliverCallback& localDeliverCallback,
                    const ErrorCallback& errorCallback) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;

  protected:
    void DoDispose() override;

  private:
    struct DecisionCacheKey
    {
        uint64_t routeEpoch;
        bool hasFiveTuple;
        EcmpFlowKey flowKey;

        bool operator<(const DecisionCacheKey& other) const;
    };

    bool TryExtractFlowKey(Ptr<const Packet> packet,
                           const Ipv4Header& header,
                           EcmpFlowKey& flowKey) const;
    void BuildHostRouteIndex();
    std::vector<EcmpRouteCandidate> FindHostCandidates(Ipv4Address destination,
                                                       Ptr<NetDevice> outputInterface,
                                                       uint32_t& countBeforeDedup);
    Ptr<Ipv4Route> BuildRoute(const EcmpRouteCandidate& candidate) const;
    Ptr<Ipv4Route> LookupPerFlow(Ptr<const Packet> packet,
                                 const Ipv4Header& header,
                                 Ptr<NetDevice> outputInterface,
                                 bool& handled);
    EcmpHrwSelection SelectCapacityAwareForwardingRoute(
        const EcmpFlowKey& flowKey,
        const std::vector<EcmpRouteCandidate>& candidates,
        std::string& selectionReason);
    void RecordDecision(const EcmpRouteDecisionEvent& event);

    RoutingMode m_selectionMode;
    uint64_t m_hashSeed;
    Ptr<FlowRouteRegistry> m_flowRouteRegistry;
    std::unique_ptr<NextHopPolicy> m_nextHopPolicy;
    bool m_hasSatelliteId;
    uint32_t m_satelliteId;
    uint64_t m_routeEpoch;
    Ptr<Ipv4> m_ipv4;
    bool m_hostRouteIndexValid;
    std::map<uint32_t, std::vector<EcmpRouteCandidate>> m_hostRouteIndex;
    std::map<DecisionCacheKey, EcmpRouteDecisionEvent> m_decisionCache;
    std::set<DecisionCacheKey> m_recordedDecisionKeys;
    TracedCallback<const EcmpRouteDecisionEvent&> m_routeDecisionTrace;
};

} // namespace ns3

#endif
