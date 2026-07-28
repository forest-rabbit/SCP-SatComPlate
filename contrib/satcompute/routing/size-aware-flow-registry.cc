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

// 维护大小感知 HRW 所需的活动流、节点级粘性选择和逻辑字节预留。

#include "size-aware-flow-registry.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <limits>
#include <tuple>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(SizeAwareFlowRegistry);

TypeId
SizeAwareFlowRegistry::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::SizeAwareFlowRegistry")
      .SetParent<Object>()
      .SetGroupName("SatCompute")
      .AddConstructor<SizeAwareFlowRegistry>();
  return typeId;
}

SizeAwareFlowRegistry::SizeAwareFlowRegistry()
  : m_totalReservedBytes(0),
    m_peakReservedBytes(0),
    m_peakCandidateReservedBytes(0),
    m_activeFlowCount(0)
{
}

SizeAwareFlowRegistry::~SizeAwareFlowRegistry()
{
}

bool
SizeAwareFlowRegistry::NodeFlowKey::operator<(
  const NodeFlowKey& other) const
{
  return std::make_tuple(nodeId, flowKey)
         < std::make_tuple(other.nodeId, other.flowKey);
}

bool
SizeAwareFlowRegistry::NodeCandidateKey::operator<(
  const NodeCandidateKey& other) const
{
  if (nodeId != other.nodeId)
    {
      return nodeId < other.nodeId;
    }
  return candidate < other.candidate;
}

void
SizeAwareFlowRegistry::RegisterTransfer(const EcmpFlowKey& flowKey,
                                        uint64_t transferId,
                                        uint64_t declaredBytes)
{
  NS_ABORT_MSG_IF(transferId == 0,
                  "size-aware registry 要求正 transfer_id");
  NS_ABORT_MSG_IF(declaredBytes == 0,
                  "size-aware registry 要求正 declared_bytes");
  SizeAwareFlowMetadata metadata = {
    transferId,
    declaredBytes,
    false
  };
  NS_ABORT_MSG_IF(!m_flows.insert(std::make_pair(flowKey, metadata)).second,
                  "size-aware registry 出现重复 five-tuple");
  NS_ABORT_MSG_IF(
    !m_flowKeysByTransferId.insert(std::make_pair(transferId, flowKey)).second,
    "size-aware registry 出现重复 transfer_id=" << transferId);
}

void
SizeAwareFlowRegistry::BeginSending(const EcmpFlowKey& flowKey)
{
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end(),
                  "size-aware registry 无法启动未登记 flow");
  NS_ABORT_MSG_IF(flow->second.senderActive,
                  "size-aware registry 重复启动 transfer_id="
                    << flow->second.transferId);
  for (const auto& assignment : m_assignments)
    {
      NS_ABORT_MSG_IF(!(assignment.first.flowKey < flowKey)
                        && !(flowKey < assignment.first.flowKey),
                      "未启动 flow 已存在 size-aware assignment");
    }
  flow->second.senderActive = true;
  NS_ABORT_MSG_IF(m_activeFlowCount
                    == std::numeric_limits<uint32_t>::max(),
                  "size-aware active flow count 溢出");
  ++m_activeFlowCount;
}

void
SizeAwareFlowRegistry::FinishSending(const EcmpFlowKey& flowKey)
{
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end(),
                  "size-aware registry 无法结束未登记 flow");
  NS_ABORT_MSG_IF(!flow->second.senderActive,
                  "size-aware registry 重复结束 transfer_id="
                    << flow->second.transferId);

  for (auto assignment = m_assignments.begin();
       assignment != m_assignments.end();)
    {
      bool sameFlow =
        !(assignment->first.flowKey < flowKey)
        && !(flowKey < assignment->first.flowKey);
      if (!sameFlow)
        {
          ++assignment;
          continue;
        }
      auto released = assignment++;
      ReleaseAssignment(released,
                        "RELEASE_SENDER_FINISHED",
                        released->second.latestRouteEpoch);
    }

  flow->second.senderActive = false;
  NS_ABORT_MSG_IF(m_activeFlowCount == 0,
                  "size-aware active flow count 下溢");
  --m_activeFlowCount;
}

bool
SizeAwareFlowRegistry::IsRegistered(const EcmpFlowKey& flowKey) const
{
  return m_flows.find(flowKey) != m_flows.end();
}

bool
SizeAwareFlowRegistry::IsSenderActive(const EcmpFlowKey& flowKey) const
{
  auto flow = m_flows.find(flowKey);
  return flow != m_flows.end() && flow->second.senderActive;
}

SizeAwareFlowMetadata
SizeAwareFlowRegistry::GetMetadata(const EcmpFlowKey& flowKey) const
{
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end(),
                  "size-aware registry 不包含请求的 flow");
  return flow->second;
}

bool
SizeAwareFlowRegistry::FindAssignment(
  uint32_t nodeId,
  const EcmpFlowKey& flowKey,
  SizeAwareFlowAssignment& assignment) const
{
  NodeFlowKey key = {
    nodeId,
    flowKey
  };
  auto found = m_assignments.find(key);
  if (found == m_assignments.end())
    {
      return false;
    }
  assignment = found->second;
  return true;
}

void
SizeAwareFlowRegistry::RecordAssignment(
  uint32_t nodeId,
  const EcmpFlowKey& flowKey,
  const EcmpRouteCandidate& candidate,
  uint64_t routeEpoch,
  const std::string& selectionReason)
{
  NS_ABORT_MSG_IF(selectionReason != "SIZE_AWARE_HRW_PRIMARY"
                    && selectionReason != "SIZE_AWARE_HRW_SECONDARY",
                  "size-aware assignment selection reason 无效");
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end() || !flow->second.senderActive,
                  "size-aware registry 只允许活动的已登记 flow 建立预留");
  NodeFlowKey assignmentKey = {
    nodeId,
    flowKey
  };
  SizeAwareFlowAssignment assignment = {
    candidate,
    flow->second.declaredBytes,
    routeEpoch
  };
  NS_ABORT_MSG_IF(
    !m_assignments.insert(
      std::make_pair(assignmentKey, assignment)).second,
    "size-aware registry 的 node+flow assignment 已存在");

  NodeCandidateKey candidateKey = {
    nodeId,
    candidate
  };
  uint64_t& candidateReserved =
    m_candidateReservedBytes[candidateKey];
  uint64_t candidateReservedBefore = candidateReserved;
  uint64_t totalReservedBefore = m_totalReservedBytes;
  NS_ABORT_MSG_IF(
    candidateReserved
      > std::numeric_limits<uint64_t>::max() - assignment.reservedBytes,
    "size-aware candidate reserved bytes 溢出");
  NS_ABORT_MSG_IF(
    m_totalReservedBytes
      > std::numeric_limits<uint64_t>::max() - assignment.reservedBytes,
    "size-aware total reserved bytes 溢出");
  candidateReserved += assignment.reservedBytes;
  m_totalReservedBytes += assignment.reservedBytes;
  if (m_totalReservedBytes > m_peakReservedBytes)
    {
      m_peakReservedBytes = m_totalReservedBytes;
    }
  if (candidateReserved > m_peakCandidateReservedBytes)
    {
      m_peakCandidateReservedBytes = candidateReserved;
    }
  RecordEvent("ASSIGN",
              selectionReason,
              routeEpoch,
              nodeId,
              flowKey,
              candidate,
              candidateReservedBefore,
              candidateReserved,
              totalReservedBefore,
              m_totalReservedBytes);
}

void
SizeAwareFlowRegistry::ValidateAssignment(uint32_t nodeId,
                                          const EcmpFlowKey& flowKey,
                                          uint64_t routeEpoch)
{
  NodeFlowKey key = {
    nodeId,
    flowKey
  };
  auto assignment = m_assignments.find(key);
  NS_ABORT_MSG_IF(assignment == m_assignments.end(),
                  "size-aware registry 无法验证不存在的 assignment");
  NS_ABORT_MSG_IF(routeEpoch < assignment->second.latestRouteEpoch,
                  "size-aware assignment route epoch 倒退");
  assignment->second.latestRouteEpoch = routeEpoch;
  uint64_t candidateReserved =
    GetReservedBytes(nodeId, assignment->second.candidate);
  RecordEvent("STICKY_REUSE",
              "SIZE_AWARE_STICKY",
              routeEpoch,
              nodeId,
              flowKey,
              assignment->second.candidate,
              candidateReserved,
              candidateReserved,
              m_totalReservedBytes,
              m_totalReservedBytes);
}

void
SizeAwareFlowRegistry::ReleaseInvalidAssignment(
  uint32_t nodeId,
  const EcmpFlowKey& flowKey,
  uint64_t routeEpoch)
{
  NodeFlowKey key = {
    nodeId,
    flowKey
  };
  auto assignment = m_assignments.find(key);
  NS_ABORT_MSG_IF(assignment == m_assignments.end(),
                  "size-aware registry 无法释放不存在的 assignment");
  ReleaseAssignment(assignment,
                    "RELEASE_CANDIDATE_INVALID",
                    routeEpoch);
}

void
SizeAwareFlowRegistry::ReleaseAssignment(
  std::map<NodeFlowKey, SizeAwareFlowAssignment>::iterator assignment,
  const std::string& action,
  uint64_t routeEpoch)
{
  NS_ABORT_MSG_IF(action != "RELEASE_CANDIDATE_INVALID"
                    && action != "RELEASE_SENDER_FINISHED",
                  "size-aware release action 无效");
  NodeCandidateKey candidateKey = {
    assignment->first.nodeId,
    assignment->second.candidate
  };
  auto candidateReserved =
    m_candidateReservedBytes.find(candidateKey);
  NS_ABORT_MSG_IF(
    candidateReserved == m_candidateReservedBytes.end()
      || candidateReserved->second < assignment->second.reservedBytes
      || m_totalReservedBytes < assignment->second.reservedBytes,
    "size-aware reserved bytes 状态不一致");

  uint64_t candidateReservedBefore = candidateReserved->second;
  uint64_t totalReservedBefore = m_totalReservedBytes;
  candidateReserved->second -= assignment->second.reservedBytes;
  m_totalReservedBytes -= assignment->second.reservedBytes;
  uint64_t candidateReservedAfter = candidateReserved->second;
  RecordEvent(action,
              "",
              routeEpoch,
              assignment->first.nodeId,
              assignment->first.flowKey,
              assignment->second.candidate,
              candidateReservedBefore,
              candidateReservedAfter,
              totalReservedBefore,
              m_totalReservedBytes);
  if (candidateReserved->second == 0)
    {
      m_candidateReservedBytes.erase(candidateReserved);
    }
  m_assignments.erase(assignment);
}

uint64_t
SizeAwareFlowRegistry::GetReservedBytes(
  uint32_t nodeId,
  const EcmpRouteCandidate& candidate) const
{
  NodeCandidateKey key = {
    nodeId,
    candidate
  };
  auto found = m_candidateReservedBytes.find(key);
  return found == m_candidateReservedBytes.end() ? 0 : found->second;
}

uint64_t
SizeAwareFlowRegistry::GetTotalReservedBytes() const
{
  return m_totalReservedBytes;
}

uint64_t
SizeAwareFlowRegistry::GetPeakReservedBytes() const
{
  return m_peakReservedBytes;
}

uint64_t
SizeAwareFlowRegistry::GetPeakCandidateReservedBytes() const
{
  return m_peakCandidateReservedBytes;
}

uint32_t
SizeAwareFlowRegistry::GetRegisteredFlowCount() const
{
  return m_flows.size();
}

uint32_t
SizeAwareFlowRegistry::GetActiveFlowCount() const
{
  return m_activeFlowCount;
}

uint32_t
SizeAwareFlowRegistry::GetAssignmentCount() const
{
  return m_assignments.size();
}

const std::vector<SizeAwareReservationEvent>&
SizeAwareFlowRegistry::GetEvents() const
{
  return m_events;
}

void
SizeAwareFlowRegistry::RecordEvent(
  const std::string& action,
  const std::string& selectionReason,
  uint64_t routeEpoch,
  uint32_t nodeId,
  const EcmpFlowKey& flowKey,
  const EcmpRouteCandidate& candidate,
  uint64_t candidateReservedBefore,
  uint64_t candidateReservedAfter,
  uint64_t totalReservedBefore,
  uint64_t totalReservedAfter)
{
  SizeAwareFlowMetadata metadata = GetMetadata(flowKey);
  SizeAwareReservationEvent event = {
    Simulator::Now().GetNanoSeconds(),
    action,
    selectionReason,
    routeEpoch,
    nodeId,
    flowKey,
    metadata.transferId,
    metadata.declaredBytes,
    candidate,
    candidateReservedBefore,
    candidateReservedAfter,
    totalReservedBefore,
    totalReservedAfter
  };
  m_events.push_back(event);
}

void
SizeAwareFlowRegistry::Clear()
{
  m_flows.clear();
  m_flowKeysByTransferId.clear();
  m_assignments.clear();
  m_candidateReservedBytes.clear();
  m_events.clear();
  m_totalReservedBytes = 0;
  m_peakReservedBytes = 0;
  m_peakCandidateReservedBytes = 0;
  m_activeFlowCount = 0;
}

} // namespace ns3
