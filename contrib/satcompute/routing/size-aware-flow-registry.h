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

#ifndef SATCOMPUTE_SIZE_AWARE_FLOW_REGISTRY_H
#define SATCOMPUTE_SIZE_AWARE_FLOW_REGISTRY_H

#include "ecmp-route-selector.h"

#include "ns3/object.h"

#include <cstdint>
#include <map>

namespace ns3 {

struct SizeAwareFlowMetadata
{
  uint64_t transferId;
  uint64_t declaredBytes;
  bool senderActive;
};

struct SizeAwareFlowAssignment
{
  EcmpRouteCandidate candidate;
  uint64_t reservedBytes;
  uint64_t latestRouteEpoch;
};

class SizeAwareFlowRegistry : public Object
{
public:
  static TypeId GetTypeId();

  SizeAwareFlowRegistry();
  ~SizeAwareFlowRegistry() override;

  void RegisterTransfer(const EcmpFlowKey& flowKey,
                        uint64_t transferId,
                        uint64_t declaredBytes);
  void BeginSending(const EcmpFlowKey& flowKey);
  void FinishSending(const EcmpFlowKey& flowKey);

  bool IsRegistered(const EcmpFlowKey& flowKey) const;
  bool IsSenderActive(const EcmpFlowKey& flowKey) const;
  SizeAwareFlowMetadata GetMetadata(const EcmpFlowKey& flowKey) const;

  bool FindAssignment(uint32_t nodeId,
                      const EcmpFlowKey& flowKey,
                      SizeAwareFlowAssignment& assignment) const;
  void RecordAssignment(uint32_t nodeId,
                        const EcmpFlowKey& flowKey,
                        const EcmpRouteCandidate& candidate,
                        uint64_t routeEpoch);
  void ValidateAssignment(uint32_t nodeId,
                          const EcmpFlowKey& flowKey,
                          uint64_t routeEpoch);
  void ReleaseAssignment(uint32_t nodeId, const EcmpFlowKey& flowKey);

  uint64_t GetReservedBytes(
    uint32_t nodeId,
    const EcmpRouteCandidate& candidate) const;
  uint64_t GetTotalReservedBytes() const;
  uint64_t GetPeakReservedBytes() const;
  uint32_t GetRegisteredFlowCount() const;
  uint32_t GetActiveFlowCount() const;
  uint32_t GetAssignmentCount() const;
  void Clear();

private:
  struct NodeFlowKey
  {
    uint32_t nodeId;
    EcmpFlowKey flowKey;

    bool operator<(const NodeFlowKey& other) const;
  };

  struct NodeCandidateKey
  {
    uint32_t nodeId;
    EcmpRouteCandidate candidate;

    bool operator<(const NodeCandidateKey& other) const;
  };

  void ReleaseAssignment(
    std::map<NodeFlowKey, SizeAwareFlowAssignment>::iterator assignment);

  std::map<EcmpFlowKey, SizeAwareFlowMetadata> m_flows;
  std::map<uint64_t, EcmpFlowKey> m_flowKeysByTransferId;
  std::map<NodeFlowKey, SizeAwareFlowAssignment> m_assignments;
  std::map<NodeCandidateKey, uint64_t> m_candidateReservedBytes;
  uint64_t m_totalReservedBytes;
  uint64_t m_peakReservedBytes;
  uint32_t m_activeFlowCount;
};

} // namespace ns3

#endif
