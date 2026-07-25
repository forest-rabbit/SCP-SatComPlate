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

#include "satcompute-ipv4-global-routing.h"

#include "fnv1a64.h"

#include "ns3/abort.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace ns3 {

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
        MakeTraceSourceAccessor(
          &SatComputeIpv4GlobalRouting::m_routeDecisionTrace),
        "ns3::TracedCallback::EcmpRouteDecisionEvent");
  return typeId;
}

SatComputeIpv4GlobalRouting::SatComputeIpv4GlobalRouting()
  : m_hashPerFlow(false),
    m_hashSeed(1),
    m_hasSatelliteId(false),
    m_satelliteId(0),
    m_routeEpoch(0),
    m_hostRouteIndexValid(false)
{
}

SatComputeIpv4GlobalRouting::~SatComputeIpv4GlobalRouting()
{
}

void
SatComputeIpv4GlobalRouting::Configure(bool hashPerFlow, uint64_t hashSeed)
{
  m_hashPerFlow = hashPerFlow;
  m_hashSeed = hashSeed;
}

void
SatComputeIpv4GlobalRouting::SetSatelliteId(uint32_t satelliteId)
{
  NS_ABORT_MSG_IF(m_hasSatelliteId,
                  "routing external satellite ID 不能重复设置");
  m_satelliteId = satelliteId;
  m_hasSatelliteId = true;
}

void
SatComputeIpv4GlobalRouting::AdvanceRouteEpoch()
{
  NS_ABORT_MSG_IF(m_routeEpoch == std::numeric_limits<uint64_t>::max(),
                  "route epoch 溢出");
  ++m_routeEpoch;
  m_hostRouteIndex.clear();
  m_hostRouteIndexValid = false;
  m_decisionCache.clear();
}

uint64_t
SatComputeIpv4GlobalRouting::GetRouteEpoch() const
{
  return m_routeEpoch;
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
  m_ipv4 = nullptr;
  Ipv4GlobalRouting::DoDispose();
}

bool
SatComputeIpv4GlobalRouting::RouteCandidate::operator<(
  const RouteCandidate& other) const
{
  return std::make_tuple(gateway.Get(),
                         outputInterface,
                         destination.Get(),
                         destinationMask.Get())
         < std::make_tuple(other.gateway.Get(),
                           other.outputInterface,
                           other.destination.Get(),
                           other.destinationMask.Get());
}

bool
SatComputeIpv4GlobalRouting::RouteCandidate::operator==(
  const RouteCandidate& other) const
{
  return gateway == other.gateway
         && outputInterface == other.outputInterface
         && destination == other.destination
         && destinationMask == other.destinationMask;
}

bool
SatComputeIpv4GlobalRouting::DecisionCacheKey::operator<(
  const DecisionCacheKey& other) const
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
SatComputeIpv4GlobalRouting::TryExtractFlowKey(
  Ptr<const Packet> packet,
  const Ipv4Header& header,
  EcmpFlowKey& flowKey) const
{
  static const uint8_t udpProtocol = 17;
  if (packet == nullptr
      || header.GetProtocol() != udpProtocol
      || header.GetSource().IsAny()
      || header.GetFragmentOffset() != 0
      || !header.IsLastFragment())
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
  NS_ABORT_MSG_IF(m_ipv4 == nullptr,
                  "SatComputeIpv4GlobalRouting 尚未绑定 Ipv4");
  m_hostRouteIndex.clear();
  for (uint32_t index = 0; index < GetNRoutes(); ++index)
    {
      Ipv4RoutingTableEntry* route = GetRoute(index);
      if (!route->IsHost())
        {
          continue;
        }

      RouteCandidate candidate = {
        route->GetGateway(),
        route->GetInterface(),
        route->GetDest(),
        route->GetDestNetworkMask()
      };
      m_hostRouteIndex[route->GetDest().Get()].push_back(candidate);
    }
  m_hostRouteIndexValid = true;
}

std::vector<SatComputeIpv4GlobalRouting::RouteCandidate>
SatComputeIpv4GlobalRouting::FindHostCandidates(
  Ipv4Address destination,
  Ptr<NetDevice> outputInterface,
  uint32_t& countBeforeDedup)
{
  if (!m_hostRouteIndexValid)
    {
      BuildHostRouteIndex();
    }

  std::vector<RouteCandidate> candidates;
  auto indexedCandidates = m_hostRouteIndex.find(destination.Get());
  if (indexedCandidates != m_hostRouteIndex.end())
    {
      for (const auto& candidate : indexedCandidates->second)
        {
          NS_ABORT_MSG_IF(candidate.outputInterface
                            >= m_ipv4->GetNInterfaces(),
                          "host route 的 output interface 越界");
          if (outputInterface == nullptr
              || outputInterface
                   == m_ipv4->GetNetDevice(candidate.outputInterface))
            {
              candidates.push_back(candidate);
            }
        }
    }
  countBeforeDedup = candidates.size();
  std::stable_sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  return candidates;
}

Ptr<Ipv4Route>
SatComputeIpv4GlobalRouting::BuildRoute(
  const RouteCandidate& candidate) const
{
  Ptr<Ipv4Route> route = Create<Ipv4Route>();
  route->SetDestination(candidate.destination);
  route->SetSource(
    m_ipv4->GetAddress(candidate.outputInterface, 0).GetLocal());
  route->SetGateway(candidate.gateway);
  route->SetOutputDevice(
    m_ipv4->GetNetDevice(candidate.outputInterface));
  return route;
}

Ptr<Ipv4Route>
SatComputeIpv4GlobalRouting::LookupPerFlow(
  Ptr<const Packet> packet,
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

  DecisionCacheKey cacheKey = {
    m_routeEpoch,
    hasFiveTuple,
    flowKey
  };
  if (outputInterface == nullptr && hasFiveTuple)
    {
      auto cached = m_decisionCache.find(cacheKey);
      if (cached != m_decisionCache.end())
        {
          const EcmpRouteDecisionEvent& event = cached->second;
          if (event.selectedIndex < 0)
            {
              return nullptr;
            }
          RouteCandidate selected = {
            event.selectedGateway,
            static_cast<uint32_t>(event.selectedOutputInterface),
            header.GetDestination(),
            Ipv4Mask("255.255.255.255")
          };
          handled = true;
          return BuildRoute(selected);
        }
    }

  uint32_t countBeforeDedup = 0;
  std::vector<RouteCandidate> candidates =
    FindHostCandidates(header.GetDestination(),
                       outputInterface,
                       countBeforeDedup);

  EcmpRouteDecisionEvent event = {};
  NS_ABORT_MSG_IF(!m_hasSatelliteId,
                  "routing 尚未配置 external satellite ID");
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
      event.selectionReason = "BASE_FALLBACK_NO_HOST_ROUTE";
      RecordDecision(event);
      return nullptr;
    }
  if (!hasFiveTuple)
    {
      event.selectionReason = "BASE_FALLBACK_NO_FIVE_TUPLE";
      RecordDecision(event);
      return nullptr;
    }

  uint64_t hashValue =
    Fnv1a64(EncodeEcmpFlowKey(m_hashSeed, flowKey));
  uint32_t selectedIndex =
    static_cast<uint32_t>(hashValue % candidates.size());
  const RouteCandidate& selected = candidates[selectedIndex];

  event.selectedIndex = selectedIndex;
  event.selectedGateway = selected.gateway;
  event.selectedOutputInterface = selected.outputInterface;
  event.hashValue = hashValue;
  event.selectionReason =
    candidates.size() == 1 ? "SINGLE_CANDIDATE" : "HASH_PER_FLOW";
  RecordDecision(event);
  handled = true;
  return BuildRoute(selected);
}

void
SatComputeIpv4GlobalRouting::RecordDecision(
  const EcmpRouteDecisionEvent& event)
{
  DecisionCacheKey key = {
    event.routeEpoch,
    event.hasFiveTuple,
    event.flowKey
  };
  auto insertion = m_decisionCache.insert(std::make_pair(key, event));
  if (!insertion.second)
    {
      const EcmpRouteDecisionEvent& first = insertion.first->second;
      NS_ABORT_MSG_IF(
        first.candidateCountBeforeDedup
            != event.candidateCountBeforeDedup
          || first.candidateCountAfterDedup
               != event.candidateCountAfterDedup
          || first.selectedIndex != event.selectedIndex
          || first.selectedGateway != event.selectedGateway
          || first.selectedOutputInterface
               != event.selectedOutputInterface
          || first.hashValue != event.hashValue
          || first.selectionReason != event.selectionReason,
        "同一 epoch、node 和 five-tuple 的路由选择发生漂移");
      return;
    }
  m_routeDecisionTrace(event);
}

Ptr<Ipv4Route>
SatComputeIpv4GlobalRouting::RouteOutput(
  Ptr<Packet> packet,
  const Ipv4Header& header,
  Ptr<NetDevice> outputInterface,
  Socket::SocketErrno& socketError)
{
  if (!m_hashPerFlow || header.GetDestination().IsMulticast())
    {
      return Ipv4GlobalRouting::RouteOutput(packet,
                                            header,
                                            outputInterface,
                                            socketError);
    }

  bool handled = false;
  Ptr<Ipv4Route> route =
    LookupPerFlow(packet, header, outputInterface, handled);
  if (!handled)
    {
      return Ipv4GlobalRouting::RouteOutput(packet,
                                            header,
                                            outputInterface,
                                            socketError);
    }
  socketError = Socket::ERROR_NOTERROR;
  return route;
}

bool
SatComputeIpv4GlobalRouting::RouteInput(
  Ptr<const Packet> packet,
  const Ipv4Header& header,
  Ptr<const NetDevice> inputDevice,
  UnicastForwardCallback unicastCallback,
  MulticastForwardCallback multicastCallback,
  LocalDeliverCallback localDeliverCallback,
  ErrorCallback errorCallback)
{
  if (!m_hashPerFlow || header.GetDestination().IsMulticast())
    {
      return Ipv4GlobalRouting::RouteInput(packet,
                                           header,
                                           inputDevice,
                                           unicastCallback,
                                           multicastCallback,
                                           localDeliverCallback,
                                           errorCallback);
    }

  int32_t inputInterface =
    m_ipv4->GetInterfaceForDevice(inputDevice);
  if (inputInterface < 0
      || m_ipv4->IsDestinationAddress(
           header.GetDestination(),
           static_cast<uint32_t>(inputInterface))
      || !m_ipv4->IsForwarding(static_cast<uint32_t>(inputInterface)))
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
  Ptr<Ipv4Route> route =
    LookupPerFlow(packet, header, nullptr, handled);
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
