/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Register every flow before simulation and own its deterministic lifecycle.

#include "network-transfer-engine.h"

#include "../routing/routing-policy-factory.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace ns3
{

void
NetworkTransferEngine::SetCapacityReservationObserver(
    Callback<void, uint32_t, uint32_t, uint64_t> observer)
{
    if (m_capacityReservationState != nullptr)
    {
        m_capacityReservationState->SetObserver(observer);
    }
}

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
        m_capacityPathPolicy = RoutingPolicyFactory::CreatePathPolicy(
            RoutingMode::CAPACITY_AWARE_HRW,
            &topology,
            m_capacityReservationState.get());
        NS_ABORT_MSG_IF(m_capacityPathPolicy == nullptr,
                        "capacity-aware routing factory returned no path policy");
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

        plan.sourceAddress =
            m_topology->GetServiceAddressBySatelliteId(plan.sourceSatelliteId);
        plan.destinationAddress =
            m_topology->GetServiceAddressBySatelliteId(plan.destinationSatelliteId);
        plan.destinationPort = NETWORK_TRANSFER_DESTINATION_PORT;
        const uint32_t ordinal = m_nextSourceOrdinal[plan.sourceSatelliteId];
        NS_ABORT_MSG_IF(ordinal > std::numeric_limits<uint16_t>::max() -
                                      NETWORK_TRANSFER_FIRST_SOURCE_PORT,
                        "one source satellite exhausted the UDP source-port range");
        plan.sourcePort =
            static_cast<uint16_t>(NETWORK_TRANSFER_FIRST_SOURCE_PORT + ordinal);
        ++m_nextSourceOrdinal[plan.sourceSatelliteId];

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
    m_states.assign(m_plans.size(), TransferRuntimeState::REGISTERED);
    m_terminalReasons.resize(m_plans.size());
    m_terminalTimesNs.assign(m_plans.size(), -1);
    m_capacityWaitStartTimesNs.assign(m_plans.size(), -1);
    m_capacityWaitingTimesNs.assign(m_plans.size(), 0);
    m_activationEvents.resize(m_plans.size());
    m_completionCallbacks.resize(m_plans.size());
    m_senders.reserve(m_plans.size());
    m_transferReceivers.reserve(m_plans.size());

    for (const NetworkTransfer& plan : m_plans)
    {
        Ptr<NetworkTransferReceiver>& receiver =
            m_receiversBySatellite[plan.destinationSatelliteId];
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
NetworkTransferEngine::RegisterRuntimePlan(NetworkTransfer plan)
{
    const int64_t now = Simulator::Now().GetNanoSeconds();
    if (!m_registered || now < 0 || now >= m_simulationDurationNs ||
        plan.transferId == 0 || m_planIndexes.count(plan.transferId) || plan.sizeBytes == 0 ||
        plan.sourceSatelliteId == plan.destinationSatelliteId ||
        !m_topology->HasSatelliteId(plan.sourceSatelliteId) ||
        !m_topology->HasSatelliteId(plan.destinationSatelliteId) ||
        m_plans.size() >= std::numeric_limits<uint32_t>::max())
    {
        throw NetworkTransferConfigError("invalid runtime transfer registration");
    }
    const auto source = m_nextSourceOrdinal.find(plan.sourceSatelliteId);
    const uint32_t ordinal = source == m_nextSourceOrdinal.end() ? 0 : source->second;
    if (ordinal > std::numeric_limits<uint16_t>::max() - NETWORK_TRANSFER_FIRST_SOURCE_PORT)
    {
        throw NetworkTransferConfigError("runtime transfer exhausted UDP source-port range");
    }
    plan.sourceAddress = m_topology->GetServiceAddressBySatelliteId(plan.sourceSatelliteId);
    plan.destinationAddress = m_topology->GetServiceAddressBySatelliteId(plan.destinationSatelliteId);
    plan.sourcePort = static_cast<uint16_t>(NETWORK_TRANSFER_FIRST_SOURCE_PORT + ordinal);
    plan.destinationPort = NETWORK_TRANSFER_DESTINATION_PORT;
    plan.arrivalTimeNs = -1;
    plan.payloadBytesPerPacket = ResolveNetworkTransferPayloadBytes(
        m_chunkMode, m_fixedPayloadBytes, plan.sizeBytes);
    plan.packetCount = plan.sizeBytes / plan.payloadBytesPerPacket +
                       (plan.sizeBytes % plan.payloadBytesPerPacket != 0);
    plan.finalPacketPayloadBytes = plan.sizeBytes % plan.payloadBytesPerPacket == 0
                                       ? plan.payloadBytesPerPacket
                                       : plan.sizeBytes % plan.payloadBytesPerPacket;

    auto& receiver = m_receiversBySatellite[plan.destinationSatelliteId];
    if (receiver == nullptr)
    {
        receiver = CreateObject<NetworkTransferReceiver>();
        receiver->Configure(plan.destinationSatelliteId, plan.destinationAddress,
                            plan.destinationPort, m_receiverRcvBufBytes, m_collectUdpSocketDrops);
        receiver->SetCompletionCallback(
            MakeCallback(&NetworkTransferEngine::HandleTransferComplete, this));
        m_topology->GetNodeBySatelliteId(plan.destinationSatelliteId)->AddApplication(receiver);
        receiver->SetStartTime(NanoSeconds(0));
        receiver->SetStopTime(NanoSeconds(m_simulationDurationNs - now));
        receiver->AddExpectedTransfer(plan);
        receiver->Initialize();
        m_receivers.push_back(receiver);
    }
    else
    {
        receiver->AddExpectedTransfer(plan);
    }
    auto sender = CreateObject<NetworkTransferApplication>();
    sender->Configure(plan);
    sender->SetSendCompleteCallback(
        MakeCallback(&NetworkTransferEngine::HandleSenderComplete, this));
    m_topology->GetNodeBySatelliteId(plan.sourceSatelliteId)->AddApplication(sender);
    sender->SetStartTime(NanoSeconds(0));
    sender->SetStopTime(NanoSeconds(m_simulationDurationNs - now));
    // Application::DoInitialize schedules relative start/stop delays. Queue startup before
    // StartTransferNow queues activation, including newly added destination receivers.
    sender->Initialize();
    if (m_flowRouteRegistry != nullptr)
    {
        m_flowRouteRegistry->RegisterTransfer(BuildNetworkTransferFlowKey(plan),
                                             plan.transferId, plan.sizeBytes);
    }
    m_planIndexes.emplace(plan.transferId, m_plans.size());
    m_runtimeTransfers.insert(plan.transferId);
    ++m_nextSourceOrdinal[plan.sourceSatelliteId];
    m_plans.push_back(plan);
    m_senders.push_back(sender);
    m_transferReceivers.push_back(receiver);
    m_states.push_back(TransferRuntimeState::REGISTERED);
    m_terminalReasons.emplace_back();
    m_terminalTimesNs.push_back(-1);
    m_capacityWaitStartTimesNs.push_back(-1);
    m_capacityWaitingTimesNs.push_back(0);
    m_activationEvents.emplace_back();
    m_completionCallbacks.emplace_back();
    m_runtimeStarting.insert(plan.transferId);
    Simulator::ScheduleNow(&NetworkTransferEngine::RuntimeApplicationsReady, this, plan.transferId);
}

void
NetworkTransferEngine::RuntimeApplicationsReady(uint64_t transferId)
{
    m_runtimeStarting.erase(transferId);
    if (m_capacityAwareRouting && !m_finalizationBatchDepth && !m_pendingCapacityTransfers.empty())
        TryActivatePendingCapacityAwareTransfers();
}

bool
NetworkTransferEngine::IsRuntimeTransfer(uint64_t transferId) const
{
    return m_runtimeTransfers.count(transferId) != 0;
}

void
NetworkTransferEngine::SetTerminalObserver(uint64_t transferId,
                                          Callback<void, uint64_t, int64_t> observer)
{
    NS_ABORT_MSG_IF(IsTerminal(transferId), "cannot observe an already terminal transfer");
    m_terminalObservers[transferId] = observer;
}

uint64_t
NetworkTransferEngine::GetReceivedBytes(uint64_t transferId) const
{
    return m_transferReceivers[GetPlanIndex(transferId)]->GetTransferReceivedBytes(transferId);
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

void
NetworkTransferEngine::StartTransferNow(
    uint64_t transferId,
    Callback<void, uint64_t, int64_t> completionCallback)
{
    NS_ABORT_MSG_IF(!m_registered, "transfer plans have not been registered");
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(m_states[index] != TransferRuntimeState::REGISTERED,
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
    if (m_capacityAwareRouting)
    {
        m_states[index] = TransferRuntimeState::WAITING_ADMISSION;
        m_capacityWaitStartTimesNs[index] = startTimeNs;
        m_pendingCapacityTransfers.push_back(transferId);
        TryActivatePendingCapacityAwareTransfers();
    }
    else
    {
        m_states[index] = TransferRuntimeState::ACTIVE;
        m_activationEvents[index] =
            Simulator::ScheduleNow(&NetworkTransferEngine::ActivateTransfer, this, transferId);
    }
}

void
NetworkTransferEngine::ActivateTransfer(uint64_t transferId)
{
    const uint32_t index = GetPlanIndex(transferId);
    if (IsTerminalTransferState(m_states[index]))
    {
        return;
    }
    NS_ABORT_MSG_IF(m_states[index] != TransferRuntimeState::ACTIVE ||
                        m_senders[index]->HasStarted(),
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
    // Capacity admission starts senders synchronously. Newly added applications must
    // finish their same-ns StartApplication events first, even when another flow finalizes.
    if (m_runtimeStarting.count(transferId)) return false;
    NS_ABORT_MSG_IF(!m_capacityAwareRouting || m_capacityPathPolicy == nullptr ||
                        m_capacityReservationState == nullptr ||
                        m_flowRouteRegistry == nullptr,
                    "capacity-aware transfer activation is not configured");
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF((m_states[index] != TransferRuntimeState::WAITING_ADMISSION &&
                     m_states[index] != TransferRuntimeState::PAUSED_ROUTE) ||
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
    const int64_t admissionTimeNs = Simulator::Now().GetNanoSeconds();
    NS_ABORT_MSG_IF(m_capacityWaitStartTimesNs[index] < 0 ||
                        admissionTimeNs < m_capacityWaitStartTimesNs[index] ||
                        m_capacityWaitingTimesNs[index] >
                            std::numeric_limits<int64_t>::max() -
                                (admissionTimeNs - m_capacityWaitStartTimesNs[index]),
                    "capacity-aware waiting-time state is invalid");
    m_capacityWaitingTimesNs[index] +=
        admissionTimeNs - m_capacityWaitStartTimesNs[index];
    m_capacityWaitStartTimesNs[index] = -1;
    m_states[index] = TransferRuntimeState::ACTIVE;
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
        if (IsTerminal(transferId))
        {
            continue;
        }
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
        if ((m_states[index] != TransferRuntimeState::ACTIVE &&
             m_states[index] != TransferRuntimeState::SENDER_FINISHED) ||
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
            m_states[index] = TransferRuntimeState::PAUSED_ROUTE;
            NS_ABORT_MSG_IF(m_capacityWaitStartTimesNs[index] >= 0,
                            "capacity-aware transfer already has an open wait interval");
            m_capacityWaitStartTimesNs[index] = Simulator::Now().GetNanoSeconds();
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
    NS_ABORT_MSG_IF(m_states[index] != TransferRuntimeState::ACTIVE,
                    "sender completion has an invalid transfer state");
    NS_ABORT_MSG_IF(sendTimeNs < m_plans[index].arrivalTimeNs ||
                        !m_senders[index]->HasFinishedSending() ||
                        m_senders[index]->GetSentBytes() != m_plans[index].sizeBytes,
                    "sender completion payload invariant failed");
    if (m_flowRouteRegistry != nullptr && !m_capacityAwareRouting)
    {
        m_flowRouteRegistry->FinishSending(GetFlowKey(index));
    }
    m_states[index] = TransferRuntimeState::SENDER_FINISHED;
}

void
NetworkTransferEngine::HandleTransferComplete(uint64_t transferId,
                                              int64_t completionTimeNs)
{
    const uint32_t index = GetPlanIndex(transferId);
    NS_ABORT_MSG_IF(IsTerminalTransferState(m_states[index]) ||
                        m_states[index] == TransferRuntimeState::REGISTERED ||
                        m_states[index] == TransferRuntimeState::WAITING_ADMISSION,
                    "receiver completion does not match one active transfer");
    NS_ABORT_MSG_IF(completionTimeNs < m_plans[index].arrivalTimeNs,
                    "receiver completion precedes transfer start");
    NS_ABORT_MSG_IF(m_transferReceivers[index]->GetTransferReceivedBytes(transferId) !=
                        m_plans[index].sizeBytes,
                    "receiver completion payload is incomplete");

    const Callback<void, uint64_t, int64_t> callback = m_completionCallbacks[index];
    NS_ABORT_MSG_IF(!FinalizeTransferIfActive(
                        transferId,
                        TransferTerminalState::COMPLETED,
                        TransferTerminalReason::RECEIVER_COMPLETED),
                    "receiver completed an already terminal transfer");
    if (!callback.IsNull())
    {
        callback(transferId, completionTimeNs);
    }
}

bool
NetworkTransferEngine::FinalizeTransferIfActive(uint64_t transferId,
                                                 TransferTerminalState terminalState,
                                                 TransferTerminalReason reason)
{
    NS_ABORT_MSG_IF(!m_registered, "transfer plans have not been registered");
    const uint32_t index = GetPlanIndex(transferId);
    if (IsTerminalTransferState(m_states[index]))
    {
        return false;
    }
    NS_ABORT_MSG_IF(terminalState == TransferTerminalState::COMPLETED &&
                        reason != TransferTerminalReason::RECEIVER_COMPLETED,
                    "completed transfer requires RECEIVER_COMPLETED reason");
    NS_ABORT_MSG_IF(terminalState != TransferTerminalState::COMPLETED &&
                        reason == TransferTerminalReason::RECEIVER_COMPLETED,
                    "failed or cancelled transfer cannot use RECEIVER_COMPLETED reason");

    const int64_t terminalTimeNs = Simulator::Now().GetNanoSeconds();
    NS_ABORT_MSG_IF(terminalTimeNs < 0,
                    "transfer terminal time cannot be negative");
    if (terminalState == TransferTerminalState::COMPLETED)
    {
        NS_ABORT_MSG_IF(m_transferReceivers[index]->GetTransferReceivedBytes(transferId) !=
                            m_plans[index].sizeBytes ||
                            m_transferReceivers[index]->GetTransferCompletionTimeNs(transferId) !=
                                terminalTimeNs,
                        "completed transfer requires exact receiver completion evidence");
    }

    if (m_activationEvents[index].IsPending())
    {
        Simulator::Cancel(m_activationEvents[index]);
    }
    NS_ABORT_MSG_IF(!m_senders[index]->FinalizeForTerminalState(),
                    "nonterminal engine transfer has a terminal sender");

    m_pendingCapacityTransfers.erase(
        std::remove(m_pendingCapacityTransfers.begin(),
                    m_pendingCapacityTransfers.end(),
                    transferId),
        m_pendingCapacityTransfers.end());
    if (m_capacityWaitStartTimesNs[index] >= 0)
    {
        NS_ABORT_MSG_IF(terminalTimeNs < m_capacityWaitStartTimesNs[index] ||
                            m_capacityWaitingTimesNs[index] >
                                std::numeric_limits<int64_t>::max() -
                                    (terminalTimeNs - m_capacityWaitStartTimesNs[index]),
                        "capacity-aware terminal waiting-time state is invalid");
        m_capacityWaitingTimesNs[index] +=
            terminalTimeNs - m_capacityWaitStartTimesNs[index];
        m_capacityWaitStartTimesNs[index] = -1;
    }
    if (m_capacityAwareRouting &&
        m_capacityReservationState->HasActivePath(transferId))
    {
        m_capacityReservationState->Release(transferId);
    }

    const EcmpFlowKey flowKey = GetFlowKey(index);
    if (m_flowRouteRegistry != nullptr && m_flowRouteRegistry->IsSenderActive(flowKey))
    {
        FlowRouteFinalizationReason flowReason =
            FlowRouteFinalizationReason::TRANSFER_COMPLETED;
        if (terminalState == TransferTerminalState::FAILED)
        {
            flowReason = FlowRouteFinalizationReason::TRANSFER_FAILED;
        }
        else if (terminalState == TransferTerminalState::CANCELLED)
        {
            flowReason = FlowRouteFinalizationReason::TRANSFER_CANCELLED;
        }
        m_flowRouteRegistry->FinalizeFlowIfActive(flowKey, flowReason);
    }
    m_topology->InvalidateFlowRouteDecisionCache(flowKey);

    m_completionCallbacks[index] = {};
    if (terminalState != TransferTerminalState::COMPLETED)
    {
        NS_ABORT_MSG_IF(!m_transferReceivers[index]->DiscardIncompleteTransfer(transferId),
                        "nonterminal engine transfer has a terminal receiver");
    }

    switch (terminalState)
    {
    case TransferTerminalState::COMPLETED:
        m_states[index] = TransferRuntimeState::COMPLETED;
        break;
    case TransferTerminalState::FAILED:
        m_states[index] = TransferRuntimeState::FAILED;
        break;
    case TransferTerminalState::CANCELLED:
        m_states[index] = TransferRuntimeState::CANCELLED;
        break;
    }
    m_terminalReasons[index] = reason;
    m_terminalTimesNs[index] = terminalTimeNs;

    const auto observer = m_terminalObservers.find(transferId);
    if (observer != m_terminalObservers.end())
    {
        const auto callback = observer->second;
        m_terminalObservers.erase(observer);
        if (!callback.IsNull())
        {
            callback(transferId, terminalTimeNs);
        }
    }

    if (m_capacityAwareRouting && !m_finalizationBatchDepth && !m_pendingCapacityTransfers.empty())
    {
        TryActivatePendingCapacityAwareTransfers();
    }
    return true;
}

uint64_t
NetworkTransferEngine::FinalizeTransfersIfActive(const std::vector<uint64_t>& ids,
                                                 TransferTerminalState state,
                                                 TransferTerminalReason reason)
{
    for (const auto id : ids) GetPlanIndex(id);
    ++m_finalizationBatchDepth;
    uint64_t count = 0;
    try
    {
        for (const auto id : ids) count += FinalizeTransferIfActive(id, state, reason);
    }
    catch (...)
    {
        --m_finalizationBatchDepth;
        throw;
    }
    --m_finalizationBatchDepth;
    if (m_capacityAwareRouting && !m_finalizationBatchDepth && !m_pendingCapacityTransfers.empty())
        TryActivatePendingCapacityAwareTransfers();
    return count;
}

bool
NetworkTransferEngine::IsCompleted(uint64_t transferId) const
{
    return m_states[GetPlanIndex(transferId)] == TransferRuntimeState::COMPLETED;
}

bool
NetworkTransferEngine::IsTerminal(uint64_t transferId) const
{
    return IsTerminalTransferState(m_states[GetPlanIndex(transferId)]);
}

TransferRuntimeState
NetworkTransferEngine::GetTransferState(uint64_t transferId) const
{
    return m_states[GetPlanIndex(transferId)];
}

std::optional<TransferTerminalReason>
NetworkTransferEngine::GetTerminalReason(uint64_t transferId) const
{
    return m_terminalReasons[GetPlanIndex(transferId)];
}

int64_t
NetworkTransferEngine::GetTerminalTimeNs(uint64_t transferId) const
{
    return m_terminalTimesNs[GetPlanIndex(transferId)];
}

uint64_t
NetworkTransferEngine::GetStalePacketCount(uint64_t transferId) const
{
    const uint32_t index = GetPlanIndex(transferId);
    return m_transferReceivers[index]->GetTransferStalePacketCount(transferId);
}

int64_t
NetworkTransferEngine::GetCapacityWaitingTimeNs(uint64_t transferId) const
{
    const uint32_t index = GetPlanIndex(transferId);
    int64_t waitingTimeNs = m_capacityWaitingTimesNs[index];
    if (m_capacityWaitStartTimesNs[index] >= 0)
    {
        const int64_t nowNs = Simulator::Now().GetNanoSeconds();
        NS_ABORT_MSG_IF(nowNs < m_capacityWaitStartTimesNs[index] ||
                            waitingTimeNs > std::numeric_limits<int64_t>::max() -
                                                (nowNs - m_capacityWaitStartTimesNs[index]),
                        "capacity-aware current waiting-time state is invalid");
        waitingTimeNs += nowNs - m_capacityWaitStartTimesNs[index];
    }
    return waitingTimeNs;
}

bool
NetworkTransferEngine::AreAllTransfersCompleted(bool includeRuntime) const
{
    if (!m_registered)
    {
        return false;
    }
    for (uint32_t index = 0; index < m_plans.size(); ++index)
    {
        if ((includeRuntime || !IsRuntimeTransfer(m_plans[index].transferId)) &&
            m_states[index] != TransferRuntimeState::COMPLETED)
        {
            return false;
        }
    }
    return true;
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
                             TransferRuntimeStateToString(m_states[index]),
                             sender->GetSentPacketCount(),
                             m_terminalTimesNs[index],
                             m_terminalReasons[index].has_value()
                                 ? TransferTerminalReasonToString(
                                       m_terminalReasons[index].value())
                                 : "",
                             receiver->GetTransferStalePacketCount(plan.transferId),
                             GetCapacityWaitingTimeNs(plan.transferId)});
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
