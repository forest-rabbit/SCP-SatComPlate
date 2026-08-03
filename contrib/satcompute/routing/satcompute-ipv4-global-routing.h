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

#ifndef SATCOMPUTE_IPV4_GLOBAL_ROUTING_H
#define SATCOMPUTE_IPV4_GLOBAL_ROUTING_H

#include "ecmp-route-selector.h"
#include "size-aware-flow-registry.h"

#include "ns3/ipv4-global-routing.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ns3 {

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

  void Configure(EcmpRouteSelectionMode selectionMode,
                 uint64_t hashSeed,
                 Ptr<SizeAwareFlowRegistry> sizeAwareRegistry);
  void SetSatelliteId(uint32_t satelliteId);
  void AdvanceRouteEpoch();
  void InvalidateDecisionCache(const EcmpFlowKey& flowKey);
  uint64_t GetRouteEpoch() const;
  std::vector<EcmpRouteCandidate> GetEffectiveRouteCandidates(
    Ipv4Address destination);

  Ptr<Ipv4Route> RouteOutput(Ptr<Packet> packet,
                             const Ipv4Header& header,
                             Ptr<NetDevice> outputInterface,
                             Socket::SocketErrno& socketError) override;
  bool RouteInput(Ptr<const Packet> packet,
                  const Ipv4Header& header,
                  Ptr<const NetDevice> inputDevice,
                  UnicastForwardCallback unicastCallback,
                  MulticastForwardCallback multicastCallback,
                  LocalDeliverCallback localDeliverCallback,
                  ErrorCallback errorCallback) override;
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
  std::vector<EcmpRouteCandidate> FindHostCandidates(
    Ipv4Address destination,
    Ptr<NetDevice> outputInterface,
    uint32_t& countBeforeDedup);
  Ptr<Ipv4Route> BuildRoute(const EcmpRouteCandidate& candidate) const;
  Ptr<Ipv4Route> LookupPerFlow(Ptr<const Packet> packet,
                               const Ipv4Header& header,
                               Ptr<NetDevice> outputInterface,
                               bool& handled);
  EcmpHrwSelection SelectSizeAwareRoute(
    const EcmpFlowKey& flowKey,
    const std::vector<EcmpRouteCandidate>& candidates,
    std::string& selectionReason);
  void RecordDecision(const EcmpRouteDecisionEvent& event);

  EcmpRouteSelectionMode m_selectionMode;
  uint64_t m_hashSeed;
  Ptr<SizeAwareFlowRegistry> m_sizeAwareRegistry;
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
