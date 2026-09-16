/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 在 ns-3 全局路由候选中执行确定性的五元组逐流 ECMP 选择。

#include "satcompute-ipv4-global-routing.h"

#include "../algorithm/hrw-per-flow-policy.h"
#include "../routing-policy-factory.h"

#include "ns3/abort.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"

#include <algorithm>
#include <limits>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(SatComputeIpv4GlobalRouting);

TypeId
SatComputeIpv4GlobalRouting::GetTypeId()
{
    static TypeId typeId =
        TypeId("ns3::SatComputeIpv4GlobalRouting")
            .SetParent<Ipv4GlobalRouting>()
            .SetGroupName("Internet")
            .AddConstructor<SatComputeIpv4GlobalRouting>()
            .AddTraceSource(
                "EcmpRouteDecision",
                "First deterministic route decision for an epoch, node, and flow key.",
                MakeTraceSourceAccessor(&SatComputeIpv4GlobalRouting::m_routeDecisionTrace),
                "ns3::TracedCallback::EcmpRouteDecisionEvent");
    return typeId;
}

SatComputeIpv4GlobalRouting::SatComputeIpv4GlobalRouting()
    : m_selectionMode(RoutingMode::GLOBAL_FIRST), m_hashSeed(1), m_hasSatelliteId(false),
      m_satelliteId(0), m_routeEpoch(0), m_hostRouteIndexValid(false)
{
}

SatComputeIpv4GlobalRouting::~SatComputeIpv4GlobalRouting()
{
}

void
SatComputeIpv4GlobalRouting::Configure(RoutingMode selectionMode,
                                       uint64_t hashSeed,
                                       Ptr<FlowRouteRegistry> flowRouteRegistry)
{
    NS_ABORT_MSG_IF(selectionMode != RoutingMode::GLOBAL_FIRST &&
                        selectionMode != RoutingMode::HASH_PER_FLOW &&
                        selectionMode != RoutingMode::HRW_PER_FLOW &&
                        selectionMode != RoutingMode::SIZE_AWARE_HRW &&
                        selectionMode != RoutingMode::CAPACITY_AWARE_HRW,
                    "SatCompute IPv4 adapter received an unsupported routing mode");
    NS_ABORT_MSG_IF(IsReservationAwareRoutingMode(selectionMode) &&
                        flowRouteRegistry == nullptr,
                    "reservation-aware HRW routing requires a flow registry");
    m_selectionMode = selectionMode;
    m_hashSeed = hashSeed;
    m_flowRouteRegistry = flowRouteRegistry;
    const SizeAwareLoadView* sizeAwareLoadView =
        flowRouteRegistry == nullptr ? nullptr : &flowRouteRegistry->GetSizeAwareLoadState();
    m_nextHopPolicy = RoutingPolicyFactory::CreateNextHopPolicy(
        selectionMode,
        PeekPointer(flowRouteRegistry),
        sizeAwareLoadView);
}

void
SatComputeIpv4GlobalRouting::SetSatelliteId(uint32_t satelliteId)
{
    NS_ABORT_MSG_IF(m_hasSatelliteId, "routing external satellite ID 不能重复设置");
    m_satelliteId = satelliteId;
    m_hasSatelliteId = true;
}

void
SatComputeIpv4GlobalRouting::AdvanceRouteEpoch()
{
    NS_ABORT_MSG_IF(m_routeEpoch == std::numeric_limits<uint64_t>::max(), "route epoch 溢出");
    ++m_routeEpoch;
    m_hostRouteIndex.clear();
    m_hostRouteIndexValid = false;
    m_decisionCache.clear();
    m_recordedDecisionKeys.clear();
}

void
SatComputeIpv4GlobalRouting::InvalidateDecisionCache(const EcmpFlowKey& flowKey)
{
    for (auto decision = m_decisionCache.begin(); decision != m_decisionCache.end();)
    {
        bool sameFlow = decision->first.hasFiveTuple && !(decision->first.flowKey < flowKey) &&
                        !(flowKey < decision->first.flowKey);
        if (sameFlow)
        {
            decision = m_decisionCache.erase(decision);
        }
        else
        {
            ++decision;
        }
    }
}

uint64_t
SatComputeIpv4GlobalRouting::GetRouteEpoch() const
{
    return m_routeEpoch;
}

std::vector<EcmpRouteCandidate>
SatComputeIpv4GlobalRouting::GetEffectiveRouteCandidates(Ipv4Address destination)
{
    uint32_t countBeforeDedup = 0;
    std::vector<EcmpRouteCandidate> candidates =
        FindHostCandidates(destination, nullptr, countBeforeDedup);
    if (!candidates.empty())
    {
        return candidates;
    }

    // ns-3 short-circuits SPF for a degree-one router and installs a default
    // network route.  Per-flow routing then falls back to the stock lookup.
    // Expose that one effective physical next hop to the read-only audit.
    for (uint32_t index = 0; index < GetNRoutes(); ++index)
    {
        Ipv4RoutingTableEntry* route = GetRoute(index);
        if (route->IsHost() ||
            !route->GetDestNetworkMask().IsMatch(destination, route->GetDestNetwork()))
        {
            continue;
        }
        candidates.push_back({route->GetGateway(),
                              route->GetInterface(),
                              route->GetDestNetwork(),
                              route->GetDestNetworkMask()});
        break;
    }
    return candidates;
}

uint32_t
SatComputeIpv4GlobalRouting::PreviewNextHop(const EcmpFlowKey& key,
                                            const std::vector<EcmpRouteCandidate>& candidates) const
{
    NS_ABORT_MSG_IF(candidates.empty(), "route preview needs candidates");
    auto scratch = CreateObject<FlowRouteRegistry>();
    scratch->RegisterTransfer(key, 1, 1);
    scratch->BeginSending(key);
    auto policy = RoutingPolicyFactory::CreateNextHopPolicy(
        m_selectionMode,
        PeekPointer(scratch),
        m_flowRouteRegistry ? &m_flowRouteRegistry->GetSizeAwareLoadState() : nullptr);
    const auto selected =
        policy->Select({m_satelliteId, m_routeEpoch, m_hashSeed, key}, candidates);
    return selected.candidateIndex;
}

void
SatComputeIpv4GlobalRouting::SetIpv4(Ptr<Ipv4> ipv4)
{
    Ipv4GlobalRouting::SetIpv4(ipv4);
    m_ipv4 = ipv4;
}

void
SatComputeIpv4GlobalRouting::DoDispose()
{
    m_hostRouteIndex.clear();
    m_decisionCache.clear();
    m_recordedDecisionKeys.clear();
    m_nextHopPolicy.reset();
    m_flowRouteRegistry = nullptr;
    m_ipv4 = nullptr;
    Ipv4GlobalRouting::DoDispose();
}

bool
SatComputeIpv4GlobalRouting::DecisionCacheKey::operator<(const DecisionCacheKey& other) const
{
    if (routeEpoch != other.routeEpoch)
    {
        return routeEpoch < other.routeEpoch;
    }
    if (hasFiveTuple != other.hasFiveTuple)
    {
        return hasFiveTuple < other.hasFiveTuple;
    }
    return flowKey < other.flowKey;
}

bool
SatComputeIpv4GlobalRouting::TryExtractFlowKey(Ptr<const Packet> packet,
                                               const Ipv4Header& header,
                                               EcmpFlowKey& flowKey) const
{
    static const uint8_t udpProtocol = 17;
    if (packet == nullptr || header.GetProtocol() != udpProtocol || header.GetSource().IsAny() ||
        header.GetFragmentOffset() != 0 || !header.IsLastFragment())
    {
        return false;
    }

    UdpHeader udpHeader;
    if (packet->PeekHeader(udpHeader) != udpHeader.GetSerializedSize())
    {
        return false;
    }

    flowKey.sourceAddress = header.GetSource();
    flowKey.destinationAddress = header.GetDestination();
    flowKey.protocol = header.GetProtocol();
    flowKey.sourcePort = udpHeader.GetSourcePort();
    flowKey.destinationPort = udpHeader.GetDestinationPort();
    return true;
}

void
SatComputeIpv4GlobalRouting::BuildHostRouteIndex()
{
    NS_ABORT_MSG_IF(m_ipv4 == nullptr, "SatComputeIpv4GlobalRouting 尚未绑定 Ipv4");
    m_hostRouteIndex.clear();
    for (uint32_t index = 0; index < GetNRoutes(); ++index)
    {
        Ipv4RoutingTableEntry* route = GetRoute(index);
        if (!route->IsHost())
        {
            continue;
        }

        EcmpRouteCandidate candidate = {route->GetGateway(),
                                        route->GetInterface(),
                                        route->GetDest(),
                                        route->GetDestNetworkMask()};
        m_hostRouteIndex[route->GetDest().Get()].push_back(candidate);
    }
    m_hostRouteIndexValid = true;
}

std::vector<EcmpRouteCandidate>
SatComputeIpv4GlobalRouting::FindHostCandidates(Ipv4Address destination,
                                                Ptr<NetDevice> outputInterface,
                                                uint32_t& countBeforeDedup)
{
    if (!m_hostRouteIndexValid)
    {
        BuildHostRouteIndex();
    }

    std::vector<EcmpRouteCandidate> candidates;
    auto indexedCandidates = m_hostRouteIndex.find(destination.Get());
    if (indexedCandidates != m_hostRouteIndex.end())
    {
        for (const auto& candidate : indexedCandidates->second)
        {
            NS_ABORT_MSG_IF(candidate.outputInterface >= m_ipv4->GetNInterfaces(),
                            "host route 的 output interface 越界");
            if (outputInterface == nullptr ||
                outputInterface == m_ipv4->GetNetDevice(candidate.outputInterface))
            {
                candidates.push_back(candidate);
            }
        }
    }
    countBeforeDedup = candidates.size();
    std::stable_sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

Ptr<Ipv4Route>
SatComputeIpv4GlobalRouting::BuildRoute(const EcmpRouteCandidate& candidate) const
{
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(candidate.destination);
    route->SetSource(m_ipv4->GetAddress(candidate.outputInterface, 0).GetLocal());
    route->SetGateway(candidate.gateway);
    route->SetOutputDevice(m_ipv4->GetNetDevice(candidate.outputInterface));
    return route;
}

EcmpHrwSelection
SatComputeIpv4GlobalRouting::SelectCapacityAwareForwardingRoute(
    const EcmpFlowKey& flowKey,
    const std::vector<EcmpRouteCandidate>& candidates,
    std::string& selectionReason)
{
    NS_ABORT_MSG_IF(candidates.empty() || m_flowRouteRegistry == nullptr ||
                        !m_flowRouteRegistry->IsSenderActive(flowKey),
                    "capacity-aware forwarding requires an active registered flow");
    NS_ABORT_MSG_IF(m_selectionMode != RoutingMode::CAPACITY_AWARE_HRW,
                    "capacity-aware forwarding used with another routing mode");

    FlowRouteAssignment sticky;
    if (m_flowRouteRegistry->FindAssignment(m_satelliteId, flowKey, sticky))
    {
        auto selected = std::find(candidates.begin(), candidates.end(), sticky.candidate);
        if (selected != candidates.end())
        {
            const uint32_t selectedIndex =
                static_cast<uint32_t>(selected - candidates.begin());
            m_flowRouteRegistry->ValidateAssignment(m_satelliteId,
                                                    flowKey,
                                                    m_routeEpoch,
                                                    "CAPACITY_AWARE_STICKY");
            selectionReason = "CAPACITY_AWARE_STICKY";
            return {selectedIndex,
                    ScoreEcmpHrwRoute(m_hashSeed, flowKey, candidates[selectedIndex])};
        }
    }

    // A packet already in flight can temporarily reach a node on a released
    // path. Forward it deterministically without creating a partial-path
    // reservation; complete-path re-admission belongs to the transfer engine.
    const EcmpHrwSelection selected = SelectEcmpHrwRoute(m_hashSeed, flowKey, candidates);
    selectionReason = "CAPACITY_AWARE_TRANSITION_FALLBACK";
    return selected;
}

Ptr<Ipv4Route>
SatComputeIpv4GlobalRouting::LookupPerFlow(Ptr<const Packet> packet,
                                           const Ipv4Header& header,
                                           Ptr<NetDevice> outputInterface,
                                           bool& handled)
{
    handled = false;
    EcmpFlowKey flowKey;
    flowKey.sourceAddress = header.GetSource();
    flowKey.destinationAddress = header.GetDestination();
    flowKey.protocol = header.GetProtocol();
    bool hasFiveTuple = TryExtractFlowKey(packet, header, flowKey);

    if (outputInterface == nullptr && hasFiveTuple)
    {
        DecisionCacheKey cacheKey = {m_routeEpoch, hasFiveTuple, flowKey};
        auto cached = m_decisionCache.find(cacheKey);
        if (cached != m_decisionCache.end())
        {
            const EcmpRouteDecisionEvent& event = cached->second;
            if (event.selectedIndex < 0)
            {
                return nullptr;
            }
            EcmpRouteCandidate selected = {event.selectedGateway,
                                           static_cast<uint32_t>(event.selectedOutputInterface),
                                           header.GetDestination(),
                                           Ipv4Mask("255.255.255.255")};
            handled = true;
            return BuildRoute(selected);
        }
    }

    uint32_t countBeforeDedup = 0;
    std::vector<EcmpRouteCandidate> candidates =
        FindHostCandidates(header.GetDestination(), outputInterface, countBeforeDedup);

    EcmpRouteDecisionEvent event = {};
    NS_ABORT_MSG_IF(!m_hasSatelliteId, "routing 尚未配置 external satellite ID");
    event.simulationTimeNs = Simulator::Now().GetNanoSeconds();
    event.routeEpoch = m_routeEpoch;
    event.nodeId = m_satelliteId;
    event.flowKey = flowKey;
    event.hasFiveTuple = hasFiveTuple;
    event.candidateCountBeforeDedup = countBeforeDedup;
    event.candidateCountAfterDedup = candidates.size();
    event.selectedIndex = -1;
    event.selectedGateway = Ipv4Address::GetAny();
    event.selectedOutputInterface = -1;

    if (candidates.empty())
    {
        if (m_selectionMode == RoutingMode::SIZE_AWARE_HRW && outputInterface == nullptr &&
            hasFiveTuple && m_flowRouteRegistry->IsSenderActive(flowKey))
        {
            FlowRouteAssignment assignment;
            if (m_flowRouteRegistry->FindAssignment(m_satelliteId, flowKey, assignment))
            {
                m_flowRouteRegistry->ReleaseInvalidAssignment(m_satelliteId,
                                                              flowKey,
                                                              m_routeEpoch);
            }
        }
        event.selectionReason = "BASE_FALLBACK_NO_HOST_ROUTE";
        if (outputInterface == nullptr)
        {
            RecordDecision(event);
        }
        return nullptr;
    }
    if (!hasFiveTuple)
    {
        event.selectionReason = "BASE_FALLBACK_NO_FIVE_TUPLE";
        if (outputInterface == nullptr)
        {
            RecordDecision(event);
        }
        return nullptr;
    }

    uint32_t selectedIndex = 0;
    uint64_t score = 0;
    std::string selectionReason;
    const bool useStatefulSizeAware = m_selectionMode == RoutingMode::SIZE_AWARE_HRW &&
                                      outputInterface == nullptr &&
                                      m_flowRouteRegistry->IsSenderActive(flowKey);
    if (m_selectionMode == RoutingMode::HASH_PER_FLOW ||
        m_selectionMode == RoutingMode::HRW_PER_FLOW || useStatefulSizeAware)
    {
        NS_ABORT_MSG_IF(m_nextHopPolicy == nullptr,
                        "per-flow lookup requires an installed deterministic policy");
        NextHopSelectionContext context = {m_satelliteId, m_routeEpoch, m_hashSeed, flowKey};
        const NextHopDecision decision = m_nextHopPolicy->Select(context, candidates);
        NS_ABORT_MSG_IF(decision.useNativeGlobalRouting ||
                            decision.candidateIndex >= candidates.size(),
                        "next-hop policy returned an invalid selection");
        selectedIndex = decision.candidateIndex;
        score = decision.score;
        selectionReason = decision.selectionReason;
    }
    else if (m_selectionMode == RoutingMode::SIZE_AWARE_HRW ||
             m_selectionMode == RoutingMode::CAPACITY_AWARE_HRW)
    {
        EcmpHrwSelection selection = {};
        if (m_selectionMode == RoutingMode::CAPACITY_AWARE_HRW && outputInterface == nullptr &&
            m_flowRouteRegistry->IsSenderActive(flowKey))
        {
            selection =
                SelectCapacityAwareForwardingRoute(flowKey, candidates, selectionReason);
        }
        else
        {
            selection = SelectEcmpHrwRoute(m_hashSeed, flowKey, candidates);
            selectionReason = m_flowRouteRegistry->IsRegistered(flowKey)
                                  ? "HRW_FALLBACK_INACTIVE"
                                  : "HRW_FALLBACK_UNREGISTERED";
        }
        selectedIndex = selection.candidateIndex;
        score = selection.score;
    }
    else
    {
        NS_ABORT_MSG("global-first 不应进入逐流候选选择");
    }
    const EcmpRouteCandidate& selected = candidates[selectedIndex];

    event.selectedIndex = selectedIndex;
    event.selectedGateway = selected.gateway;
    event.selectedOutputInterface = selected.outputInterface;
    event.hashValue = score;
    event.selectionReason = candidates.size() == 1 ? "SINGLE_CANDIDATE" : selectionReason;
    if (outputInterface == nullptr)
    {
        RecordDecision(event);
    }
    handled = true;
    return BuildRoute(selected);
}

void
SatComputeIpv4GlobalRouting::RecordDecision(const EcmpRouteDecisionEvent& event)
{
    DecisionCacheKey key = {event.routeEpoch, event.hasFiveTuple, event.flowKey};
    auto insertion = m_decisionCache.insert(std::make_pair(key, event));
    if (!insertion.second)
    {
        const EcmpRouteDecisionEvent& first = insertion.first->second;
        NS_ABORT_MSG_IF(first.candidateCountBeforeDedup != event.candidateCountBeforeDedup ||
                            first.candidateCountAfterDedup != event.candidateCountAfterDedup ||
                            first.selectedIndex != event.selectedIndex ||
                            first.selectedGateway != event.selectedGateway ||
                            first.selectedOutputInterface != event.selectedOutputInterface ||
                            first.hashValue != event.hashValue ||
                            first.selectionReason != event.selectionReason,
                        "同一 epoch、node 和 five-tuple 的路由选择发生漂移");
        return;
    }
    if (m_recordedDecisionKeys.insert(key).second)
    {
        m_routeDecisionTrace(event);
    }
}

Ptr<Ipv4Route>
SatComputeIpv4GlobalRouting::RouteOutput(Ptr<Packet> packet,
                                         const Ipv4Header& header,
                                         Ptr<NetDevice> outputInterface,
                                         Socket::SocketErrno& socketError)
{
    if (m_selectionMode == RoutingMode::GLOBAL_FIRST || header.GetDestination().IsMulticast())
    {
        return Ipv4GlobalRouting::RouteOutput(packet, header, outputInterface, socketError);
    }

    bool handled = false;
    Ptr<Ipv4Route> route = LookupPerFlow(packet, header, outputInterface, handled);
    if (!handled)
    {
        return Ipv4GlobalRouting::RouteOutput(packet, header, outputInterface, socketError);
    }
    socketError = Socket::ERROR_NOTERROR;
    return route;
}

bool
SatComputeIpv4GlobalRouting::RouteInput(Ptr<const Packet> packet,
                                        const Ipv4Header& header,
                                        Ptr<const NetDevice> inputDevice,
                                        const UnicastForwardCallback& unicastCallback,
                                        const MulticastForwardCallback& multicastCallback,
                                        const LocalDeliverCallback& localDeliverCallback,
                                        const ErrorCallback& errorCallback)
{
    if (m_selectionMode == RoutingMode::GLOBAL_FIRST || header.GetDestination().IsMulticast())
    {
        return Ipv4GlobalRouting::RouteInput(packet,
                                             header,
                                             inputDevice,
                                             unicastCallback,
                                             multicastCallback,
                                             localDeliverCallback,
                                             errorCallback);
    }

    int32_t inputInterface = m_ipv4->GetInterfaceForDevice(inputDevice);
    if (inputInterface < 0 ||
        m_ipv4->IsDestinationAddress(header.GetDestination(),
                                     static_cast<uint32_t>(inputInterface)) ||
        !m_ipv4->IsForwarding(static_cast<uint32_t>(inputInterface)))
    {
        return Ipv4GlobalRouting::RouteInput(packet,
                                             header,
                                             inputDevice,
                                             unicastCallback,
                                             multicastCallback,
                                             localDeliverCallback,
                                             errorCallback);
    }

    bool handled = false;
    Ptr<Ipv4Route> route = LookupPerFlow(packet, header, nullptr, handled);
    if (!handled)
    {
        return Ipv4GlobalRouting::RouteInput(packet,
                                             header,
                                             inputDevice,
                                             unicastCallback,
                                             multicastCallback,
                                             localDeliverCallback,
                                             errorCallback);
    }
    unicastCallback(route, packet, header);
    return true;
}

} // namespace ns3
