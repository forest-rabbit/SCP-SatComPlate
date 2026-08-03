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

// 维护 reservation-aware 路由共用的 flow 生命周期、粘性选择和事件。

#include "flow-route-registry.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <limits>
#include <tuple>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(FlowRouteRegistry);

TypeId
FlowRouteRegistry::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::FlowRouteRegistry")
      .SetParent<Object>()
      .SetGroupName("SatCompute")
      .AddConstructor<FlowRouteRegistry>();
  return typeId;
}

FlowRouteRegistry::FlowRouteRegistry()
  : m_activeFlowCount(0)
{
}

FlowRouteRegistry::~FlowRouteRegistry()
{
}

bool
FlowRouteRegistry::NodeFlowKey::operator<(
  const NodeFlowKey& other) const
{
  return std::make_tuple(nodeId, flowKey)
         < std::make_tuple(other.nodeId, other.flowKey);
}

void
FlowRouteRegistry::RegisterTransfer(const EcmpFlowKey& flowKey,
                                    uint64_t transferId,
                                    uint64_t declaredBytes)
{
  NS_ABORT_MSG_IF(transferId == 0,
                  "flow route registry 要求正 transfer_id");
  NS_ABORT_MSG_IF(declaredBytes == 0,
                  "flow route registry 要求正 declared_bytes");
  FlowRouteMetadata metadata = {
    transferId,
    declaredBytes,
    false
  };
  NS_ABORT_MSG_IF(!m_flows.insert(std::make_pair(flowKey, metadata)).second,
                  "flow route registry 出现重复 five-tuple");
  NS_ABORT_MSG_IF(
    !m_flowKeysByTransferId.insert(std::make_pair(transferId, flowKey)).second,
    "flow route registry 出现重复 transfer_id=" << transferId);
}

void
FlowRouteRegistry::BeginSending(const EcmpFlowKey& flowKey)
{
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end(),
                  "flow route registry 无法启动未登记 flow");
  NS_ABORT_MSG_IF(flow->second.senderActive,
                  "flow route registry 重复启动 transfer_id="
                    << flow->second.transferId);
  for (const auto& assignment : m_assignments)
    {
      NS_ABORT_MSG_IF(!(assignment.first.flowKey < flowKey)
                        && !(flowKey < assignment.first.flowKey),
                      "未启动 flow 已存在 flow route assignment");
    }
  flow->second.senderActive = true;
  NS_ABORT_MSG_IF(m_activeFlowCount
                    == std::numeric_limits<uint32_t>::max(),
                  "flow route active flow count 溢出");
  ++m_activeFlowCount;
}

void
FlowRouteRegistry::FinishSending(const EcmpFlowKey& flowKey)
{
  FinishFlow(flowKey, "RELEASE_SENDER_FINISHED");
}

void
FlowRouteRegistry::FinishReceiving(const EcmpFlowKey& flowKey)
{
  FinishFlow(flowKey, "RELEASE_TRANSFER_COMPLETED");
}

void
FlowRouteRegistry::FinishFlow(const EcmpFlowKey& flowKey,
                                  const std::string& releaseAction)
{
  NS_ABORT_MSG_IF(releaseAction != "RELEASE_SENDER_FINISHED"
                    && releaseAction != "RELEASE_TRANSFER_COMPLETED",
                  "flow route release action 无效");
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end(),
                  "flow route registry 无法结束未登记 flow");
  NS_ABORT_MSG_IF(!flow->second.senderActive,
                  "flow route registry 重复结束 transfer_id="
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
                        releaseAction,
                        released->second.latestRouteEpoch);
    }

  flow->second.senderActive = false;
  NS_ABORT_MSG_IF(m_activeFlowCount == 0,
                  "flow route active flow count 下溢");
  --m_activeFlowCount;
}

bool
FlowRouteRegistry::IsRegistered(const EcmpFlowKey& flowKey) const
{
  return m_flows.find(flowKey) != m_flows.end();
}

bool
FlowRouteRegistry::IsSenderActive(const EcmpFlowKey& flowKey) const
{
  auto flow = m_flows.find(flowKey);
  return flow != m_flows.end() && flow->second.senderActive;
}

FlowRouteMetadata
FlowRouteRegistry::GetMetadata(const EcmpFlowKey& flowKey) const
{
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end(),
                  "flow route registry 不包含请求的 flow");
  return flow->second;
}

bool
FlowRouteRegistry::FindAssignment(
  uint32_t nodeId,
  const EcmpFlowKey& flowKey,
  FlowRouteAssignment& assignment) const
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
FlowRouteRegistry::RecordAssignment(
  uint32_t nodeId,
  const EcmpFlowKey& flowKey,
  const EcmpRouteCandidate& candidate,
  uint64_t routeEpoch,
  const std::string& selectionReason)
{
  NS_ABORT_MSG_IF(selectionReason != "SIZE_AWARE_HRW_PRIMARY"
                    && selectionReason != "SIZE_AWARE_HRW_SECONDARY"
                    && selectionReason != "CAPACITY_AWARE_PATH",
                  "flow route assignment selection reason 无效");
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end() || !flow->second.senderActive,
                  "flow route registry 只允许活动的已登记 flow 建立预留");
  NodeFlowKey assignmentKey = {
    nodeId,
    flowKey
  };
  FlowRouteAssignment assignment = {
    candidate,
    flow->second.declaredBytes,
    routeEpoch
  };
  NS_ABORT_MSG_IF(
    !m_assignments.insert(
      std::make_pair(assignmentKey, assignment)).second,
    "flow route registry 的 node+flow assignment 已存在");

  SizeAwareLoadChange loadChange =
    m_sizeAwareLoadState.Reserve(nodeId,
                                 candidate,
                                 assignment.reservedBytes);
  RecordEvent("ASSIGN",
              selectionReason,
              routeEpoch,
              nodeId,
              flowKey,
              candidate,
              loadChange.candidateReservedBefore,
              loadChange.candidateReservedAfter,
              loadChange.totalReservedBefore,
              loadChange.totalReservedAfter);
}

void
FlowRouteRegistry::ValidateAssignment(uint32_t nodeId,
                                      const EcmpFlowKey& flowKey,
                                      uint64_t routeEpoch,
                                      const std::string& selectionReason)
{
  NS_ABORT_MSG_IF(selectionReason != "SIZE_AWARE_STICKY"
                    && selectionReason != "CAPACITY_AWARE_STICKY",
                  "flow route sticky selection reason 无效");
  NodeFlowKey key = {
    nodeId,
    flowKey
  };
  auto assignment = m_assignments.find(key);
  NS_ABORT_MSG_IF(assignment == m_assignments.end(),
                  "flow route registry 无法验证不存在的 assignment");
  NS_ABORT_MSG_IF(routeEpoch < assignment->second.latestRouteEpoch,
                  "flow route assignment route epoch 倒退");
  assignment->second.latestRouteEpoch = routeEpoch;
  uint64_t candidateReserved =
    GetReservedBytes(nodeId, assignment->second.candidate);
  RecordEvent("STICKY_REUSE",
              selectionReason,
              routeEpoch,
              nodeId,
              flowKey,
              assignment->second.candidate,
              candidateReserved,
              candidateReserved,
              m_sizeAwareLoadState.GetTotalReservedBytes(),
              m_sizeAwareLoadState.GetTotalReservedBytes());
}

void
FlowRouteRegistry::ReleaseInvalidAssignment(
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
                  "flow route registry 无法释放不存在的 assignment");
  ReleaseAssignment(assignment,
                    "RELEASE_CANDIDATE_INVALID",
                    routeEpoch);
}

void
FlowRouteRegistry::ReleaseAssignmentsForRouteUpdate(
  const EcmpFlowKey& flowKey,
  uint64_t routeEpoch)
{
  auto flow = m_flows.find(flowKey);
  NS_ABORT_MSG_IF(flow == m_flows.end() || !flow->second.senderActive,
                  "flow route registry 只能为活动 flow 释放旧路径");
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
                        "RELEASE_ROUTE_INVALIDATED",
                        routeEpoch);
    }
}

void
FlowRouteRegistry::ReleaseAssignment(
  std::map<NodeFlowKey, FlowRouteAssignment>::iterator assignment,
  const std::string& action,
  uint64_t routeEpoch)
{
  NS_ABORT_MSG_IF(action != "RELEASE_CANDIDATE_INVALID"
                    && action != "RELEASE_SENDER_FINISHED"
                    && action != "RELEASE_TRANSFER_COMPLETED"
                    && action != "RELEASE_ROUTE_INVALIDATED",
                  "flow route release action 无效");
  SizeAwareLoadChange loadChange =
    m_sizeAwareLoadState.Release(assignment->first.nodeId,
                                 assignment->second.candidate,
                                 assignment->second.reservedBytes);
  RecordEvent(action,
              "",
              routeEpoch,
              assignment->first.nodeId,
              assignment->first.flowKey,
              assignment->second.candidate,
              loadChange.candidateReservedBefore,
              loadChange.candidateReservedAfter,
              loadChange.totalReservedBefore,
              loadChange.totalReservedAfter);
  m_assignments.erase(assignment);
}

const SizeAwareLoadState&
FlowRouteRegistry::GetSizeAwareLoadState() const
{
  return m_sizeAwareLoadState;
}

uint64_t
FlowRouteRegistry::GetReservedBytes(
  uint32_t nodeId,
  const EcmpRouteCandidate& candidate) const
{
  return m_sizeAwareLoadState.GetReservedBytes(nodeId, candidate);
}

uint64_t
FlowRouteRegistry::GetTotalReservedBytes() const
{
  return m_sizeAwareLoadState.GetTotalReservedBytes();
}

uint64_t
FlowRouteRegistry::GetPeakReservedBytes() const
{
  return m_sizeAwareLoadState.GetPeakReservedBytes();
}

uint64_t
FlowRouteRegistry::GetPeakCandidateReservedBytes() const
{
  return m_sizeAwareLoadState.GetPeakCandidateReservedBytes();
}

uint32_t
FlowRouteRegistry::GetRegisteredFlowCount() const
{
  return m_flows.size();
}

uint32_t
FlowRouteRegistry::GetActiveFlowCount() const
{
  return m_activeFlowCount;
}

uint32_t
FlowRouteRegistry::GetAssignmentCount() const
{
  return m_assignments.size();
}

const std::vector<FlowRouteReservationEvent>&
FlowRouteRegistry::GetEvents() const
{
  return m_events;
}

void
FlowRouteRegistry::RecordEvent(
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
  FlowRouteMetadata metadata = GetMetadata(flowKey);
  FlowRouteReservationEvent event = {
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
FlowRouteRegistry::Clear()
{
  m_flows.clear();
  m_flowKeysByTransferId.clear();
  m_assignments.clear();
  m_sizeAwareLoadState.Clear();
  m_events.clear();
  m_activeFlowCount = 0;
}

} // namespace ns3
