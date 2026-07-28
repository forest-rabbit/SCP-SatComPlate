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

// 统一预注册传输计划，并在运行时按需启动和回调完整接收事件。

#include "network-transfer-engine.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(NetworkTransferEngine);

namespace {

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
  NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                  "NetworkTransfer " << field << " 溢出");
  return left + right;
}

EcmpFlowKey
BuildFlowKey(const NetworkTransfer& transfer)
{
  EcmpFlowKey flowKey;
  flowKey.sourceAddress = transfer.sourceAddress;
  flowKey.destinationAddress = transfer.destinationAddress;
  flowKey.protocol = 17;
  flowKey.sourcePort = transfer.sourcePort;
  flowKey.destinationPort = transfer.destinationPort;
  return flowKey;
}

} // namespace

TypeId
NetworkTransferEngine::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::NetworkTransferEngine")
      .SetParent<Object>()
      .SetGroupName("SatCompute")
      .AddConstructor<NetworkTransferEngine>();
  return typeId;
}

NetworkTransferEngine::NetworkTransferEngine()
  : m_topology(nullptr),
    m_fixedPayloadBytes(0),
    m_islMtuBytes(0),
    m_receiverRcvBufBytes(0),
    m_collectUdpSocketDrops(false),
    m_simulationDurationNs(0),
    m_configured(false),
    m_registered(false)
{
}

NetworkTransferEngine::~NetworkTransferEngine()
{
}

void
NetworkTransferEngine::Configure(const SatelliteTopology& topology,
                                 const std::string& chunkMode,
                                 uint32_t fixedPayloadBytes,
                                 uint16_t islMtuBytes,
                                 uint32_t receiverRcvBufBytes,
                                 bool collectUdpSocketDrops,
                                 double simulationDurationSeconds)
{
  NS_ABORT_MSG_IF(m_configured || m_registered,
                  "NetworkTransferEngine 只能配置一次");
  NS_ABORT_MSG_IF(chunkMode != "fixed" && chunkMode != "size-aware",
                  "transferChunkMode 必须是 fixed 或 size-aware");
  NS_ABORT_MSG_IF(fixedPayloadBytes == 0 || fixedPayloadBytes > 65507,
                  "transferPayloadBytes 必须在 1..65507 范围内");
  NS_ABORT_MSG_IF(islMtuBytes < 68,
                  "islMtuBytes 必须至少为 68");
  NS_ABORT_MSG_IF(receiverRcvBufBytes == 0,
                  "receiverRcvBufBytes 必须大于 0");
  uint32_t maximumPayloadBytes =
    chunkMode == "size-aware"
      ? GetSizeAwareMaximumPayloadBytes()
      : fixedPayloadBytes;
  NS_ABORT_MSG_IF(maximumPayloadBytes + 28u > islMtuBytes,
                  "maximum transfer payload plus UDP/IPv4 headers "
                  "must fit islMtuBytes");

  int64_t durationNs = Seconds(simulationDurationSeconds).GetNanoSeconds();
  NS_ABORT_MSG_IF(durationNs <= 0,
                  "simulationDuration 无法表示为正的纳秒时长");
  m_topology = &topology;
  m_chunkMode = chunkMode;
  m_fixedPayloadBytes = fixedPayloadBytes;
  m_islMtuBytes = islMtuBytes;
  m_receiverRcvBufBytes = receiverRcvBufBytes;
  m_collectUdpSocketDrops = collectUdpSocketDrops;
  m_simulationDurationNs = durationNs;
  m_sizeAwareRegistry = topology.GetSizeAwareFlowRegistry();
  m_configured = true;
}

void
NetworkTransferEngine::RegisterPlans(std::vector<NetworkTransfer> plans)
{
  NS_ABORT_MSG_IF(!m_configured,
                  "NetworkTransferEngine 必须先 Configure");
  NS_ABORT_MSG_IF(m_registered,
                  "NetworkTransferEngine plans 只能注册一次");
  NS_ABORT_MSG_IF(plans.empty(),
                  "NetworkTransferEngine 至少需要一个 plan");

  std::sort(plans.begin(),
            plans.end(),
            [](const NetworkTransfer& left, const NetworkTransfer& right) {
              return left.transferId < right.transferId;
            });

  std::map<uint32_t, uint32_t> nextSourceOrdinal;
  for (uint32_t index = 0; index < plans.size(); ++index)
    {
      NetworkTransfer& plan = plans[index];
      NS_ABORT_MSG_IF(plan.transferId == 0,
                      "NetworkTransfer plan 要求正 transfer_id");
      NS_ABORT_MSG_IF(
        !m_planIndexes.insert(std::make_pair(plan.transferId, index)).second,
        "NetworkTransfer plans 包含重复 transfer_id=" << plan.transferId);
      NS_ABORT_MSG_IF(plan.sourceSatelliteId == plan.destinationSatelliteId,
                      "NetworkTransfer 源卫星和目的卫星不能相同，transfer_id="
                        << plan.transferId);
      NS_ABORT_MSG_IF(!m_topology->HasSatelliteId(plan.sourceSatelliteId),
                      "NetworkTransfer plan 引用了未知源卫星 "
                        << plan.sourceSatelliteId
                        << "，transfer_id=" << plan.transferId);
      NS_ABORT_MSG_IF(!m_topology->HasSatelliteId(plan.destinationSatelliteId),
                      "NetworkTransfer plan 引用了未知目的卫星 "
                        << plan.destinationSatelliteId
                        << "，transfer_id=" << plan.transferId);
      NS_ABORT_MSG_IF(plan.sizeBytes == 0,
                      "NetworkTransfer size_bytes 必须为正，transfer_id="
                        << plan.transferId);
      NS_ABORT_MSG_IF(plan.arrivalTimeNs < -1
                        || plan.arrivalTimeNs >= m_simulationDurationNs,
                      "NetworkTransfer plan start time 必须为 -1 或早于 "
                      "simulation stop，transfer_id=" << plan.transferId);

      plan.sourceAddress =
        m_topology->GetServiceAddressBySatelliteId(plan.sourceSatelliteId);
      plan.destinationAddress =
        m_topology->GetServiceAddressBySatelliteId(plan.destinationSatelliteId);
      plan.destinationPort = NETWORK_TRANSFER_DESTINATION_PORT;
      uint32_t ordinal = nextSourceOrdinal[plan.sourceSatelliteId];
      NS_ABORT_MSG_IF(
        ordinal
          > std::numeric_limits<uint16_t>::max()
              - NETWORK_TRANSFER_FIRST_SOURCE_PORT,
        "同一源卫星的 NetworkTransfer 数量超出 UDP source port 空间: "
          << plan.sourceSatelliteId);
      plan.sourcePort = static_cast<uint16_t>(
        NETWORK_TRANSFER_FIRST_SOURCE_PORT + ordinal);
      ++nextSourceOrdinal[plan.sourceSatelliteId];

      plan.payloadBytesPerPacket =
        ResolveNetworkTransferPayloadBytes(m_chunkMode,
                                           m_fixedPayloadBytes,
                                           plan.sizeBytes);
      plan.packetCount =
        plan.sizeBytes / plan.payloadBytesPerPacket
        + (plan.sizeBytes % plan.payloadBytesPerPacket == 0 ? 0 : 1);
      plan.finalPacketPayloadBytes =
        plan.sizeBytes % plan.payloadBytesPerPacket == 0
          ? plan.payloadBytesPerPacket
          : static_cast<uint32_t>(
              plan.sizeBytes % plan.payloadBytesPerPacket);

      if (m_sizeAwareRegistry != nullptr)
        {
          m_sizeAwareRegistry->RegisterTransfer(BuildFlowKey(plan),
                                                plan.transferId,
                                                plan.sizeBytes);
        }
    }

  m_plans = plans;
  m_states.assign(m_plans.size(), TRANSFER_REGISTERED);
  m_completionCallbacks.resize(m_plans.size());
  m_senders.reserve(m_plans.size());
  m_transferReceivers.reserve(m_plans.size());

  std::map<uint32_t, Ptr<NetworkTransferReceiver>> receiversBySatellite;
  for (const auto& plan : m_plans)
    {
      Ptr<NetworkTransferReceiver>& receiver =
        receiversBySatellite[plan.destinationSatelliteId];
      if (receiver == nullptr)
        {
          receiver = CreateObject<NetworkTransferReceiver>();
          receiver->Configure(plan.destinationSatelliteId,
                              plan.destinationAddress,
                              plan.destinationPort,
                              m_receiverRcvBufBytes,
                              m_collectUdpSocketDrops);
          receiver->SetCompletionCallback(
            MakeCallback(&NetworkTransferEngine::HandleTransferComplete, this));
          m_topology->GetNodeBySatelliteId(plan.destinationSatelliteId)
            ->AddApplication(receiver);
          receiver->SetStartTime(NanoSeconds(0));
          receiver->SetStopTime(NanoSeconds(m_simulationDurationNs));
          m_receivers.push_back(receiver);
        }
      receiver->AddExpectedTransfer(plan);
      m_transferReceivers.push_back(receiver);
    }

  for (const auto& plan : m_plans)
    {
      Ptr<NetworkTransferApplication> sender =
        CreateObject<NetworkTransferApplication>();
      sender->Configure(plan);
      sender->SetSendCompleteCallback(
        MakeCallback(&NetworkTransferEngine::HandleSenderComplete, this));
      m_topology->GetNodeBySatelliteId(plan.sourceSatelliteId)
        ->AddApplication(sender);
      sender->SetStartTime(NanoSeconds(0));
      sender->SetStopTime(NanoSeconds(m_simulationDurationNs));
      m_senders.push_back(sender);
    }
  m_registered = true;
}

uint32_t
NetworkTransferEngine::GetPlanIndex(uint64_t transferId) const
{
  auto plan = m_planIndexes.find(transferId);
  NS_ABORT_MSG_IF(plan == m_planIndexes.end(),
                  "NetworkTransferEngine 不包含 transfer_id=" << transferId);
  return plan->second;
}

EcmpFlowKey
NetworkTransferEngine::GetFlowKey(uint32_t index) const
{
  NS_ABORT_MSG_IF(index >= m_plans.size(),
                  "NetworkTransfer flow key 下标越界");
  return BuildFlowKey(m_plans[index]);
}

const char*
NetworkTransferEngine::GetTransferStateName(uint32_t index) const
{
  NS_ABORT_MSG_IF(index >= m_states.size(),
                  "NetworkTransfer state 下标越界");
  switch (m_states[index])
    {
    case TRANSFER_REGISTERED:
      return "REGISTERED";
    case TRANSFER_STARTED:
      return "STARTED";
    case TRANSFER_COMPLETED:
      return "COMPLETED";
    }
  return "UNKNOWN";
}

void
NetworkTransferEngine::StartTransferNow(
  uint64_t transferId,
  Callback<void, uint64_t, int64_t> completionCallback)
{
  NS_ABORT_MSG_IF(!m_registered,
                  "NetworkTransferEngine plans 尚未注册");
  uint32_t index = GetPlanIndex(transferId);
  NS_ABORT_MSG_IF(m_states[index] != TRANSFER_REGISTERED,
                  "NetworkTransfer 只能启动一次，transfer_id=" << transferId);

  int64_t startTimeNs = Simulator::Now().GetNanoSeconds();
  NS_ABORT_MSG_IF(startTimeNs < 0 || startTimeNs >= m_simulationDurationNs,
                  "NetworkTransfer start time 必须早于 simulation stop，"
                  "transfer_id=" << transferId);
  NS_ABORT_MSG_IF(m_plans[index].arrivalTimeNs >= 0
                    && m_plans[index].arrivalTimeNs != startTimeNs,
                  "NetworkTransfer 未在声明 arrival_time_ns 启动，transfer_id="
                    << transferId
                    << " declared=" << m_plans[index].arrivalTimeNs
                    << " actual=" << startTimeNs);

  m_plans[index].arrivalTimeNs = startTimeNs;
  m_completionCallbacks[index] = completionCallback;
  m_transferReceivers[index]->MarkTransferStarted(transferId, startTimeNs);
  m_states[index] = TRANSFER_STARTED;
  Simulator::ScheduleNow(&NetworkTransferEngine::ActivateTransfer,
                         this,
                         transferId);
}

void
NetworkTransferEngine::ActivateTransfer(uint64_t transferId)
{
  uint32_t index = GetPlanIndex(transferId);
  NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED
                    || m_senders[index]->HasStarted(),
                  "NetworkTransfer sender activation 状态无效，transfer_id="
                    << transferId);
  if (m_sizeAwareRegistry != nullptr)
    {
      m_sizeAwareRegistry->BeginSending(GetFlowKey(index));
    }
  m_senders[index]->StartTransferNow();
}

void
NetworkTransferEngine::HandleSenderComplete(uint64_t transferId,
                                            int64_t sendTimeNs)
{
  uint32_t index = GetPlanIndex(transferId);
  NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED,
                  "NetworkTransfer sender completion 状态无效，transfer_id="
                    << transferId);
  NS_ABORT_MSG_IF(sendTimeNs < m_plans[index].arrivalTimeNs
                    || !m_senders[index]->HasFinishedSending()
                    || m_senders[index]->GetSentBytes()
                         != m_plans[index].sizeBytes,
                  "NetworkTransfer sender completion payload 无效，transfer_id="
                    << transferId);
  if (m_sizeAwareRegistry != nullptr)
    {
      m_sizeAwareRegistry->FinishSending(GetFlowKey(index));
    }
}

void
NetworkTransferEngine::HandleTransferComplete(uint64_t transferId,
                                              int64_t completionTimeNs)
{
  uint32_t index = GetPlanIndex(transferId);
  NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED,
                  "NetworkTransfer completion 未对应唯一已启动传输，"
                  "transfer_id=" << transferId);
  NS_ABORT_MSG_IF(completionTimeNs < m_plans[index].arrivalTimeNs,
                  "NetworkTransfer completion 早于 start，transfer_id="
                    << transferId);
  NS_ABORT_MSG_IF(
    m_transferReceivers[index]->GetTransferReceivedBytes(transferId)
      != m_plans[index].sizeBytes,
    "NetworkTransfer completion payload 不完整，transfer_id=" << transferId);

  m_states[index] = TRANSFER_COMPLETED;
  Callback<void, uint64_t, int64_t> callback = m_completionCallbacks[index];
  m_completionCallbacks[index] = Callback<void, uint64_t, int64_t>();
  if (!callback.IsNull())
    {
      callback(transferId, completionTimeNs);
    }
}

bool
NetworkTransferEngine::IsCompleted(uint64_t transferId) const
{
  return m_states[GetPlanIndex(transferId)] == TRANSFER_COMPLETED;
}

bool
NetworkTransferEngine::AreAllTransfersCompleted() const
{
  return m_registered
         && std::all_of(m_states.begin(),
                        m_states.end(),
                        [](TransferState state) {
                          return state == TRANSFER_COMPLETED;
                        });
}

const std::vector<NetworkTransfer>&
NetworkTransferEngine::GetPlans() const
{
  NS_ABORT_MSG_IF(!m_registered,
                  "NetworkTransferEngine plans 尚未注册");
  return m_plans;
}

ApplicationMetrics
NetworkTransferEngine::CollectApplicationMetrics() const
{
  NS_ABORT_MSG_IF(!m_registered,
                  "NetworkTransferEngine plans 尚未注册");
  ApplicationMetrics metrics = {};
  metrics.sinkApplications = m_receivers.size();
  bool requireComplete = AreAllTransfersCompleted();
  NS_ABORT_MSG_IF(m_senders.size() != m_plans.size(),
                  "NetworkTransfer sender 与 plan 数量不一致");
  for (uint32_t index = 0; index < m_senders.size(); ++index)
    {
      const Ptr<NetworkTransferApplication>& sender = m_senders[index];
      const NetworkTransfer& plan = m_plans[index];
      NS_ABORT_MSG_IF(sender->GetTransferId() != plan.transferId,
                      "NetworkTransfer sender 顺序与 plan 不一致");
      if (requireComplete)
        {
          NS_ABORT_MSG_IF(!sender->HasStarted()
                            || sender->GetSentBytes() != plan.sizeBytes
                            || sender->GetSentPacketCount() != plan.packetCount,
                          "NetworkTransfer sender 未完成计划 payload，"
                          "transfer_id=" << plan.transferId);
        }
      metrics.sentBytes =
        CheckedAdd(metrics.sentBytes, sender->GetSentBytes(), "sent bytes");
    }
  for (const auto& receiver : m_receivers)
    {
      metrics.receivedBytes =
        CheckedAdd(metrics.receivedBytes,
                   receiver->GetTotalReceivedBytes(),
                   "received bytes");
    }
  return metrics;
}

std::vector<TransferFlowMetadata>
NetworkTransferEngine::CollectFlowMetadata() const
{
  NS_ABORT_MSG_IF(m_plans.size() != m_transferReceivers.size(),
                  "NetworkTransfer 与 receiver 映射数量不一致");
  std::vector<TransferFlowMetadata> metadata;
  metadata.reserve(m_plans.size());
  for (uint32_t index = 0; index < m_plans.size(); ++index)
    {
      const NetworkTransfer& plan = m_plans[index];
      TransferFlowMetadata flow = {
        plan.transferId,
        plan.sourceAddress,
        plan.destinationAddress,
        17,
        plan.sourcePort,
        plan.destinationPort,
        plan.sizeBytes,
        m_transferReceivers[index]->GetTransferReceivedBytes(plan.transferId)
      };
      metadata.push_back(flow);
    }
  return metadata;
}

std::vector<TransferSummaryRecord>
NetworkTransferEngine::CollectSummaries() const
{
  NS_ABORT_MSG_IF(m_plans.size() != m_senders.size()
                    || m_plans.size() != m_transferReceivers.size(),
                  "NetworkTransfer summary 映射数量不一致");
  std::vector<TransferSummaryRecord> summaries;
  summaries.reserve(m_plans.size());
  for (uint32_t index = 0; index < m_plans.size(); ++index)
    {
      const NetworkTransfer& plan = m_plans[index];
      const Ptr<NetworkTransferApplication>& sender = m_senders[index];
      const Ptr<NetworkTransferReceiver>& receiver =
        m_transferReceivers[index];
      NS_ABORT_MSG_IF(sender->GetTransferId() != plan.transferId,
                      "NetworkTransfer summary sender 顺序不一致");

      uint64_t receivedBytes =
        receiver->GetTransferReceivedBytes(plan.transferId);
      int64_t completionTimeNs =
        receiver->GetTransferCompletionTimeNs(plan.transferId);
      int64_t completionDelayNs = -1;
      if (completionTimeNs >= 0)
        {
          NS_ABORT_MSG_IF(receivedBytes != plan.sizeBytes,
                          "已完成 NetworkTransfer 的接收字节不等于声明值，"
                          "transfer_id=" << plan.transferId);
          completionDelayNs = completionTimeNs - plan.arrivalTimeNs;
          NS_ABORT_MSG_IF(completionDelayNs < 0,
                          "NetworkTransfer completion delay 为负，transfer_id="
                            << plan.transferId);
        }

      TransferSummaryRecord summary = {
        plan.transferId,
        plan.sourceSatelliteId,
        plan.destinationSatelliteId,
        plan.sourceAddress,
        plan.destinationAddress,
        plan.sourcePort,
        plan.destinationPort,
        plan.sizeBytes,
        plan.payloadBytesPerPacket,
        "first-hop-serialization",
        plan.packetCount,
        plan.finalPacketPayloadBytes,
        plan.arrivalTimeNs,
        sender->GetLastSendTimeNs(),
        sender->GetSentBytes(),
        receivedBytes,
        receiver->GetTransferReceivedPacketCount(plan.transferId),
        completionTimeNs,
        completionDelayNs,
        GetTransferStateName(index),
        sender->GetSentPacketCount()
      };
      summaries.push_back(summary);
    }
  return summaries;
}

std::vector<UdpSocketDropEvent>
NetworkTransferEngine::CollectUdpSocketDropEvents() const
{
  std::vector<UdpSocketDropEvent> events;
  for (const auto& receiver : m_receivers)
    {
      const std::vector<UdpSocketDropEvent>& receiverEvents =
        receiver->GetUdpSocketDropEvents();
      events.insert(events.end(),
                    receiverEvents.begin(),
                    receiverEvents.end());
    }
  std::sort(
    events.begin(),
    events.end(),
    [](const UdpSocketDropEvent& left,
       const UdpSocketDropEvent& right) {
      return std::make_tuple(left.simulationTimeNs,
                             left.destinationSatelliteId,
                             left.destinationAddress.Get(),
                             left.destinationPort,
                             left.cumulativeDropPackets)
             < std::make_tuple(right.simulationTimeNs,
                               right.destinationSatelliteId,
                               right.destinationAddress.Get(),
                               right.destinationPort,
                               right.cumulativeDropPackets);
    });
  return events;
}

} // namespace ns3
