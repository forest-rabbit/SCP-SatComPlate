/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Register every flow before simulation and own its deterministic lifecycle.

#include "network-transfer-engine.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(NetworkTransferEngine);

namespace
{

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
    NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                    "NetworkTransfer " << field << " overflow");
    return left + right;
}

} // namespace

TypeId
NetworkTransferEngine::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::NetworkTransferEngine")
                               .SetParent<Object>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<NetworkTransferEngine>();
    return typeId;
}

NetworkTransferEngine::NetworkTransferEngine() = default;

NetworkTransferEngine::~NetworkTransferEngine() = default;

void
NetworkTransferEngine::Configure(SatelliteRuntimeView& topology,
                                 const std::string& chunkMode,
                                 uint32_t fixedPayloadBytes,
                                 uint16_t islMtuBytes,
                                 uint32_t receiverRcvBufBytes,
                                 bool collectUdpSocketDrops,
                                 int64_t simulationDurationNs)
{
    NS_ABORT_MSG_IF(m_configured || m_registered,
                    "NetworkTransferEngine can only be configured once");
    NS_ABORT_MSG_IF(chunkMode != "fixed" && chunkMode != "size-aware",
                    "transfer chunk mode must be fixed or size-aware");
    NS_ABORT_MSG_IF(fixedPayloadBytes == 0 || fixedPayloadBytes > 65507,
                    "fixed transfer payload must be in [1, 65507]");
    NS_ABORT_MSG_IF(islMtuBytes < 68, "ISL MTU must be at least 68 bytes");
    NS_ABORT_MSG_IF(receiverRcvBufBytes == 0, "receiver buffer must be positive");
    const uint32_t maximumPayloadBytes = chunkMode == "size-aware"
                                             ? GetSizeAwareMaximumPayloadBytes()
                                             : fixedPayloadBytes;
    NS_ABORT_MSG_IF(maximumPayloadBytes + 28u > islMtuBytes,
                    "maximum transfer payload plus UDP/IPv4 headers exceeds the ISL MTU");
    NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                    "simulation duration must be positive integer nanoseconds");

    m_topology = &topology;
    m_chunkMode = chunkMode;
    m_fixedPayloadBytes = fixedPayloadBytes;
    m_islMtuBytes = islMtuBytes;
    m_receiverRcvBufBytes = receiverRcvBufBytes;
    m_collectUdpSocketDrops = collectUdpSocketDrops;
    m_simulationDurationNs = simulationDurationNs;
    m_flowRouteRegistry = topology.GetFlowRouteRegistry();
    m_capacityAwareRouting = topology.IsCapacityAwareRouting();
    if (m_capacityAwareRouting)
    {
        NS_ABORT_MSG_IF(m_flowRouteRegistry == nullptr,
                        "capacity-aware routing requires a flow registry");
        m_capacityReservationState = std::make_unique<CapacityReservationState>();
        m_capacityPathPolicy = std::make_unique<CapacityAwareHrwPolicy>(
            topology,
            *m_capacityReservationState);
        topology.RegisterRouteUpdateCallback(
            MakeCallback(&NetworkTransferEngine::HandleTopologyRouteUpdate, this));
    }
    m_configured = true;
}

void
NetworkTransferEngine::RegisterPlans(std::vector<NetworkTransfer> plans)
{
    NS_ABORT_MSG_IF(!m_configured, "NetworkTransferEngine must be configured first");
    NS_ABORT_MSG_IF(m_registered, "transfer plans can only be registered once");
    NS_ABORT_MSG_IF(plans.empty(), "at least one transfer plan is required");

    std::sort(plans.begin(),
              plans.end(),
              [](const NetworkTransfer& left, const NetworkTransfer& right) {
                  return left.transferId < right.transferId;
              });

    std::map<uint32_t, uint32_t> nextSourceOrdinal;
    for (uint32_t index = 0; index < plans.size(); ++index)
    {
        NetworkTransfer& plan = plans[index];
        NS_ABORT_MSG_IF(plan.transferId == 0, "transfer plan requires a positive ID");
        NS_ABORT_MSG_IF(!m_planIndexes.emplace(plan.transferId, index).second,
                        "transfer plans contain a duplicate ID");
        NS_ABORT_MSG_IF(plan.sourceSatelliteId == plan.destinationSatelliteId,
                        "transfer source and destination must differ");
        NS_ABORT_MSG_IF(!m_topology->HasSatelliteId(plan.sourceSatelliteId) ||
                            !m_topology->HasSatelliteId(plan.destinationSatelliteId),
                        "transfer plan references an unknown satellite");
        NS_ABORT_MSG_IF(plan.sizeBytes == 0, "transfer size must be positive");
        NS_ABORT_MSG_IF(plan.arrivalTimeNs < -1 ||
                            plan.arrivalTimeNs >= m_simulationDurationNs,
                        "transfer start time must be -1 or precede simulation stop");

        plan.sourceAddress = m_topology->GetServiceAddress(plan.sourceSatelliteId);
        plan.destinationAddress = m_topology->GetServiceAddress(plan.destinationSatelliteId);
        plan.destinationPort = NETWORK_TRANSFER_DESTINATION_PORT;
        const uint32_t ordinal = nextSourceOrdinal[plan.sourceSatelliteId];
        NS_ABORT_MSG_IF(ordinal > std::numeric_limits<uint16_t>::max() -
                                      NETWORK_TRANSFER_FIRST_SOURCE_PORT,
                        "one source satellite exhausted the UDP source-port range");
        plan.sourcePort =
            static_cast<uint16_t>(NETWORK_TRANSFER_FIRST_SOURCE_PORT + ordinal);
        ++nextSourceOrdinal[plan.sourceSatelliteId];

        plan.payloadBytesPerPacket = ResolveNetworkTransferPayloadBytes(
            m_chunkMode,
            m_fixedPayloadBytes,
            plan.sizeBytes);
        plan.packetCount = plan.sizeBytes / plan.payloadBytesPerPacket +
                           (plan.sizeBytes % plan.payloadBytesPerPacket == 0 ? 0 : 1);
        plan.finalPacketPayloadBytes =
            plan.sizeBytes % plan.payloadBytesPerPacket == 0
                ? plan.payloadBytesPerPacket
                : static_cast<uint32_t>(plan.sizeBytes % plan.payloadBytesPerPacket);

        if (m_flowRouteRegistry != nullptr)
        {
            m_flowRouteRegistry->RegisterTransfer(BuildNetworkTransferFlowKey(plan),
                                                  plan.transferId,
                                                  plan.sizeBytes);
        }
    }

    m_plans = std::move(plans);
    m_states.assign(m_plans.size(), TRANSFER_REGISTERED);
    m_completionCallbacks.resize(m_plans.size());
    m_senders.reserve(m_plans.size());
    m_transferReceivers.reserve(m_plans.size());

    std::map<uint32_t, Ptr<NetworkTransferReceiver>> receiversBySatellite;
    for (const NetworkTransfer& plan : m_plans)
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

    for (const NetworkTransfer& plan : m_plans)
    {
        Ptr<NetworkTransferApplication> sender =
            CreateObject<NetworkTransferApplication>();
        sender->Configure(plan);
        sender->SetSendCompleteCallback(
            MakeCallback(&NetworkTransferEngine::HandleSenderComplete, this));
        m_topology->GetNodeBySatelliteId(plan.sourceSatelliteId)->AddApplication(sender);
        sender->SetStartTime(NanoSeconds(0));
        sender->SetStopTime(NanoSeconds(m_simulationDurationNs));
        m_senders.push_back(sender);
    }
    m_registered = true;
}

void
NetworkTransferEngine::ScheduleDeclaredTransfers()
{
    NS_ABORT_MSG_IF(!m_registered, "transfer plans have not been registered");
    NS_ABORT_MSG_IF(m_declaredTransfersScheduled,
                    "declared transfers can only be scheduled once");
    for (const NetworkTransfer& plan : m_plans)
    {
        NS_ABORT_MSG_IF(plan.arrivalTimeNs < 0,
                        "declared transfer scheduling requires an arrival time");
        Simulator::Schedule(NanoSeconds(plan.arrivalTimeNs),
                            &NetworkTransferEngine::StartDeclaredTransfer,
                            this,
                            plan.transferId);
    }
    m_declaredTransfersScheduled = true;
}

uint32_t
NetworkTransferEngine::GetPlanIndex(uint64_t transferId) const
{
    const auto plan = m_planIndexes.find(transferId);
    NS_ABORT_MSG_IF(plan == m_planIndexes.end(), "NetworkTransferEngine has no transfer ID");
    return plan->second;
}

EcmpFlowKey
NetworkTransferEngine::GetFlowKey(uint32_t index) const
{
    NS_ABORT_MSG_IF(index >= m_plans.size(), "transfer flow-key index is out of range");
    return BuildNetworkTransferFlowKey(m_plans[index]);
}

const char*
NetworkTransferEngine::GetTransferStateName(uint32_t index) const
{
    NS_ABORT_MSG_IF(index >= m_states.size(), "transfer state index is out of range");
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
NetworkTransferEngine::StartDeclaredTransfer(uint64_t transferId)
{
    StartTransferNow(transferId, Callback<void, uint64_t, int64_t>());
}

void
NetworkTransferEngine::StartTransferNow(
    uint64_t transferId,
    Callback<void, uint64_t, int64_t> completionCallback)
{
    NS_ABORT_MSG_IF(!m_registered, "transfer plans have not been registered");
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(m_states[index] != TRANSFER_REGISTERED,
                    "a transfer can only start once");

    const int64_t startTimeNs = Simulator::Now().GetNanoSeconds();
    NS_ABORT_MSG_IF(startTimeNs < 0 || startTimeNs >= m_simulationDurationNs,
                    "transfer start must precede simulation stop");
    NS_ABORT_MSG_IF(m_plans[index].arrivalTimeNs >= 0 &&
                        m_plans[index].arrivalTimeNs != startTimeNs,
                    "transfer did not start at its declared arrival time");

    m_plans[index].arrivalTimeNs = startTimeNs;
    m_completionCallbacks[index] = completionCallback;
    m_transferReceivers[index]->MarkTransferStarted(transferId, startTimeNs);
    m_states[index] = TRANSFER_STARTED;
    if (m_capacityAwareRouting)
    {
        m_pendingCapacityTransfers.push_back(transferId);
        TryActivatePendingCapacityAwareTransfers();
    }
    else
    {
        Simulator::ScheduleNow(&NetworkTransferEngine::ActivateTransfer, this, transferId);
    }
}

void
NetworkTransferEngine::ActivateTransfer(uint64_t transferId)
{
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED || m_senders[index]->HasStarted(),
                    "transfer sender activation state is invalid");
    if (m_flowRouteRegistry != nullptr)
    {
        m_flowRouteRegistry->BeginSending(GetFlowKey(index));
    }
    m_senders[index]->StartTransferNow();
}

bool
NetworkTransferEngine::TryActivateCapacityAwareTransfer(uint64_t transferId)
{
    NS_ABORT_MSG_IF(!m_capacityAwareRouting || m_capacityPathPolicy == nullptr ||
                        m_capacityReservationState == nullptr ||
                        m_flowRouteRegistry == nullptr,
                    "capacity-aware transfer activation is not configured");
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED ||
                        m_capacityReservationState->HasActivePath(transferId),
                    "capacity-aware sender activation state is invalid");
    Ptr<NetworkTransferApplication> sender = m_senders[index];
    const bool firstAdmission = !sender->HasStarted();
    NS_ABORT_MSG_IF(!firstAdmission && !sender->HasFinishedSending() &&
                        !sender->IsPausedForRouteUpdate(),
                    "capacity-aware readmission requires a paused sender");

    const EcmpFlowKey flowKey = GetFlowKey(index);
    CapacityAwarePath path;
    const PathSelectionContext context = {flowKey,
                                          m_plans[index].sourceSatelliteId,
                                          m_plans[index].destinationSatelliteId,
                                          m_topology->GetHashSeed()};
    if (!m_capacityPathPolicy->FindPath(context, path))
    {
        return false;
    }

    if (firstAdmission)
    {
        m_flowRouteRegistry->BeginSending(flowKey);
    }
    for (const CapacityAwarePathHop& hop : path.hops)
    {
        if (hop.candidate.destinationMask != Ipv4Mask("255.255.255.255"))
        {
            continue;
        }
        m_flowRouteRegistry->RecordAssignment(hop.sourceSatelliteId,
                                              flowKey,
                                              hop.candidate,
                                              m_topology->GetRouteEpoch(
                                                  hop.sourceSatelliteId),
                                              "CAPACITY_AWARE_PATH");
    }
    m_capacityReservationState->Reserve(transferId, path);
    if (firstAdmission)
    {
        sender->SetPacingRateBps(path.admittedRateBps);
        sender->StartTransferNow();
    }
    else if (!sender->HasFinishedSending())
    {
        m_topology->InvalidateFlowRouteDecisionCache(flowKey);
        sender->ResumeAfterRouteUpdate(path.admittedRateBps);
    }
    return true;
}

void
NetworkTransferEngine::TryActivatePendingCapacityAwareTransfers()
{
    NS_ABORT_MSG_IF(!m_capacityAwareRouting,
                    "non-capacity-aware run has pending admission state");
    std::vector<uint64_t> stillPending;
    stillPending.reserve(m_pendingCapacityTransfers.size());
    for (const uint64_t transferId : m_pendingCapacityTransfers)
    {
        if (!TryActivateCapacityAwareTransfer(transferId))
        {
            stillPending.push_back(transferId);
        }
    }
    m_pendingCapacityTransfers.swap(stillPending);
}

void
NetworkTransferEngine::HandleTopologyRouteUpdate()
{
    NS_ABORT_MSG_IF(!m_capacityAwareRouting || !m_registered,
                    "capacity-aware route-update state is invalid");
    std::vector<uint64_t> invalidTransfers;
    for (uint32_t index = 0; index < m_plans.size(); ++index)
    {
        const uint64_t transferId = m_plans[index].transferId;
        if (m_states[index] != TRANSFER_STARTED ||
            !m_capacityReservationState->HasActivePath(transferId) ||
            m_capacityReservationState->IsActivePathValid(
                transferId,
                m_plans[index].destinationSatelliteId,
                *m_topology))
        {
            continue;
        }
        invalidTransfers.push_back(transferId);
    }

    std::sort(invalidTransfers.begin(),
              invalidTransfers.end(),
              [this](uint64_t leftId, uint64_t rightId) {
                  const NetworkTransfer& left = m_plans[GetPlanIndex(leftId)];
                  const NetworkTransfer& right = m_plans[GetPlanIndex(rightId)];
                  return std::make_pair(left.arrivalTimeNs, left.transferId) <
                         std::make_pair(right.arrivalTimeNs, right.transferId);
              });

    std::vector<uint64_t> readmissionTransfers;
    for (const uint64_t transferId : invalidTransfers)
    {
        const uint32_t index = GetPlanIndex(transferId);
        Ptr<NetworkTransferApplication> sender = m_senders[index];
        if (!sender->HasFinishedSending())
        {
            sender->PauseForRouteUpdate();
            readmissionTransfers.push_back(transferId);
        }
        const EcmpFlowKey flowKey = GetFlowKey(index);
        m_flowRouteRegistry->ReleaseAssignmentsForRouteUpdate(
            flowKey,
            m_topology->GetRouteEpoch(m_plans[index].sourceSatelliteId));
        m_capacityReservationState->Release(transferId);
    }

    if (invalidTransfers.empty())
    {
        if (!m_pendingCapacityTransfers.empty())
        {
            TryActivatePendingCapacityAwareTransfers();
        }
        return;
    }
    std::vector<uint64_t> pending = readmissionTransfers;
    for (const uint64_t transferId : m_pendingCapacityTransfers)
    {
        if (std::find(invalidTransfers.begin(), invalidTransfers.end(), transferId) ==
            invalidTransfers.end())
        {
            pending.push_back(transferId);
        }
    }
    m_pendingCapacityTransfers.swap(pending);
    TryActivatePendingCapacityAwareTransfers();
}

void
NetworkTransferEngine::HandleSenderComplete(uint64_t transferId, int64_t sendTimeNs)
{
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED,
                    "sender completion has an invalid transfer state");
    NS_ABORT_MSG_IF(sendTimeNs < m_plans[index].arrivalTimeNs ||
                        !m_senders[index]->HasFinishedSending() ||
                        m_senders[index]->GetSentBytes() != m_plans[index].sizeBytes,
                    "sender completion payload invariant failed");
    if (m_flowRouteRegistry != nullptr && !m_capacityAwareRouting)
    {
        m_flowRouteRegistry->FinishSending(GetFlowKey(index));
    }
}

void
NetworkTransferEngine::HandleTransferComplete(uint64_t transferId,
                                              int64_t completionTimeNs)
{
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(m_states[index] != TRANSFER_STARTED,
                    "receiver completion does not match one started transfer");
    NS_ABORT_MSG_IF(completionTimeNs < m_plans[index].arrivalTimeNs,
                    "receiver completion precedes transfer start");
    NS_ABORT_MSG_IF(m_transferReceivers[index]->GetTransferReceivedBytes(transferId) !=
                        m_plans[index].sizeBytes,
                    "receiver completion payload is incomplete");

    if (m_capacityAwareRouting)
    {
        m_flowRouteRegistry->FinishReceiving(GetFlowKey(index));
        if (m_capacityReservationState->HasActivePath(transferId))
        {
            m_capacityReservationState->Release(transferId);
        }
        m_pendingCapacityTransfers.erase(
            std::remove(m_pendingCapacityTransfers.begin(),
                        m_pendingCapacityTransfers.end(),
                        transferId),
            m_pendingCapacityTransfers.end());
    }
    m_states[index] = TRANSFER_COMPLETED;
    if (m_capacityAwareRouting)
    {
        TryActivatePendingCapacityAwareTransfers();
    }
    const Callback<void, uint64_t, int64_t> callback = m_completionCallbacks[index];
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
    return m_registered &&
           std::all_of(m_states.begin(), m_states.end(), [](TransferState state) {
               return state == TRANSFER_COMPLETED;
           });
}

const std::vector<NetworkTransfer>&
NetworkTransferEngine::GetPlans() const
{
    NS_ABORT_MSG_IF(!m_registered, "transfer plans have not been registered");
    return m_plans;
}

ApplicationMetrics
NetworkTransferEngine::CollectApplicationMetrics() const
{
    NS_ABORT_MSG_IF(!m_registered, "transfer plans have not been registered");
    ApplicationMetrics metrics = {m_receivers.size(), 0, 0};
    const bool requireComplete = AreAllTransfersCompleted();
    NS_ABORT_MSG_IF(m_senders.size() != m_plans.size(),
                    "transfer sender and plan counts differ");
    for (uint32_t index = 0; index < m_senders.size(); ++index)
    {
        const Ptr<NetworkTransferApplication>& sender = m_senders[index];
        const NetworkTransfer& plan = m_plans[index];
        NS_ABORT_MSG_IF(sender->GetTransferId() != plan.transferId,
                        "transfer sender order differs from plan order");
        if (requireComplete)
        {
            NS_ABORT_MSG_IF(!sender->HasStarted() || sender->GetSentBytes() != plan.sizeBytes ||
                                sender->GetSentPacketCount() != plan.packetCount,
                            "completed run has an incomplete sender");
        }
        metrics.sentBytes = CheckedAdd(metrics.sentBytes, sender->GetSentBytes(), "sent bytes");
    }
    for (const Ptr<NetworkTransferReceiver>& receiver : m_receivers)
    {
        metrics.receivedBytes = CheckedAdd(metrics.receivedBytes,
                                           receiver->GetTotalReceivedBytes(),
                                           "received bytes");
    }
    return metrics;
}

std::vector<TransferFlowMetadata>
NetworkTransferEngine::CollectFlowMetadata() const
{
    NS_ABORT_MSG_IF(m_plans.size() != m_transferReceivers.size(),
                    "transfer receiver mapping count differs");
    std::vector<TransferFlowMetadata> metadata;
    metadata.reserve(m_plans.size());
    for (uint32_t index = 0; index < m_plans.size(); ++index)
    {
        const NetworkTransfer& plan = m_plans[index];
        metadata.push_back({plan.transferId,
                            plan.sourceAddress,
                            plan.destinationAddress,
                            17,
                            plan.sourcePort,
                            plan.destinationPort,
                            plan.sizeBytes,
                            m_transferReceivers[index]->GetTransferReceivedBytes(
                                plan.transferId)});
    }
    return metadata;
}

std::vector<TransferSummaryRecord>
NetworkTransferEngine::CollectSummaries() const
{
    NS_ABORT_MSG_IF(m_plans.size() != m_senders.size() ||
                        m_plans.size() != m_transferReceivers.size(),
                    "transfer summary mapping count differs");
    std::vector<TransferSummaryRecord> summaries;
    summaries.reserve(m_plans.size());
    for (uint32_t index = 0; index < m_plans.size(); ++index)
    {
        const NetworkTransfer& plan = m_plans[index];
        const Ptr<NetworkTransferApplication>& sender = m_senders[index];
        const Ptr<NetworkTransferReceiver>& receiver = m_transferReceivers[index];
        NS_ABORT_MSG_IF(sender->GetTransferId() != plan.transferId,
                        "transfer summary sender order differs");

        const uint64_t receivedBytes = receiver->GetTransferReceivedBytes(plan.transferId);
        const int64_t completionTimeNs =
            receiver->GetTransferCompletionTimeNs(plan.transferId);
        int64_t completionDelayNs = -1;
        if (completionTimeNs >= 0)
        {
            NS_ABORT_MSG_IF(receivedBytes != plan.sizeBytes,
                            "completed transfer does not contain its declared bytes");
            completionDelayNs = completionTimeNs - plan.arrivalTimeNs;
            NS_ABORT_MSG_IF(completionDelayNs < 0, "transfer completion delay is negative");
        }

        summaries.push_back({plan.transferId,
                             plan.sourceSatelliteId,
                             plan.destinationSatelliteId,
                             plan.sourceAddress,
                             plan.destinationAddress,
                             plan.sourcePort,
                             plan.destinationPort,
                             plan.sizeBytes,
                             plan.payloadBytesPerPacket,
                             m_capacityAwareRouting ? "path-bottleneck-serialization"
                                                    : "first-hop-serialization",
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
                             sender->GetSentPacketCount()});
    }
    return summaries;
}

std::vector<UdpSocketDropEvent>
NetworkTransferEngine::CollectUdpSocketDropEvents() const
{
    std::vector<UdpSocketDropEvent> events;
    for (const Ptr<NetworkTransferReceiver>& receiver : m_receivers)
    {
        const std::vector<UdpSocketDropEvent>& receiverEvents =
            receiver->GetUdpSocketDropEvents();
        events.insert(events.end(), receiverEvents.begin(), receiverEvents.end());
    }
    std::sort(events.begin(),
              events.end(),
              [](const UdpSocketDropEvent& left, const UdpSocketDropEvent& right) {
                  return std::make_tuple(left.simulationTimeNs,
                                         left.destinationSatelliteId,
                                         left.destinationAddress.Get(),
                                         left.destinationPort,
                                         left.cumulativeDropPackets) <
                         std::make_tuple(right.simulationTimeNs,
                                         right.destinationSatelliteId,
                                         right.destinationAddress.Get(),
                                         right.destinationPort,
                                         right.cumulativeDropPackets);
              });
    return events;
}

CapacityAwareRuntimeSummary
NetworkTransferEngine::CollectCapacityAwareSummary() const
{
    NS_ABORT_MSG_IF(!m_configured || !m_capacityAwareRouting ||
                        m_capacityReservationState == nullptr,
                    "capacity-aware summary is not configured");
    CapacityAwareRuntimeSummary summary = m_capacityReservationState->CollectSummary();
    summary.pendingTransferCountAtEnd = m_pendingCapacityTransfers.size();
    return summary;
}

} // namespace ns3
