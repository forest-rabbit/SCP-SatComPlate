/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Apply all fault events at one timestamp as one deterministic transaction.

#include "fault-controller.h"

#include "../task/task-coordinator.h"
#include "../topology/satellite-topology-controller.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(FaultController);

namespace
{

uint8_t
GetEventPriority(FaultEventType eventType)
{
    switch (eventType)
    {
    case FaultEventType::NOTICE:
        return 0;
    case FaultEventType::NOTICE_CLEAR:
        return 1;
    case FaultEventType::RECOVERY:
        return 2;
    case FaultEventType::START:
        return 3;
    }
    return 4;
}

FaultDefinition
MakeTimeGatedEventFault(FaultEventType eventType, const FaultDefinition& source)
{
    FaultDefinition event = source;
    if (eventType == FaultEventType::NOTICE)
    {
        event.faultOccurred = false;
        event.startTimeNs = std::nullopt;
        event.warningLeadTimeNs = std::nullopt;
        event.riskDurationNs = std::nullopt;
        event.durationNs = std::nullopt;
    }
    else if (eventType == FaultEventType::NOTICE_CLEAR)
    {
        event.faultOccurred = false;
        event.startTimeNs = std::nullopt;
        event.warningLeadTimeNs = std::nullopt;
        event.durationNs = std::nullopt;
    }
    return event;
}

void
ValidateGeneratedEvent(const GeneratedFaultEvent& event,
                       int64_t nowNs,
                       int64_t simulationDurationNs,
                       const std::set<uint32_t>& satelliteIds)
{
    const FaultDefinition& fault = event.fault;
    if (event.eventType == FaultEventType::RECOVERY)
    {
        throw FaultControllerError("generated RECOVERY events are scheduled by FaultController");
    }
    if (fault.faultId == 0)
    {
        throw FaultControllerError("generated fault_id must be positive");
    }
    if (!satelliteIds.contains(fault.nodeId))
    {
        throw FaultControllerError("generated fault references an unknown satellite ID");
    }
    if (fault.failureProbability.has_value() &&
        (!std::isfinite(fault.failureProbability.value()) ||
         fault.failureProbability.value() < 0.0 ||
         fault.failureProbability.value() > 1.0))
    {
        throw FaultControllerError(
            "generated failure_probability must be finite and in [0, 1]");
    }

    if (event.eventType == FaultEventType::NOTICE)
    {
        if (fault.faultType != FaultType::COMPUTE || fault.faultOccurred ||
            fault.noticeTimeNs != nowNs || fault.startTimeNs.has_value() ||
            !fault.failureProbability.has_value() ||
            fault.warningLeadTimeNs.has_value() || fault.riskDurationNs.has_value() ||
            fault.durationNs.has_value())
        {
            throw FaultControllerError(
                "generated NOTICE must contain only current compute risk information");
        }
        return;
    }

    if (event.eventType == FaultEventType::NOTICE_CLEAR)
    {
        if (fault.faultType != FaultType::COMPUTE || fault.faultOccurred ||
            !fault.noticeTimeNs.has_value() || fault.startTimeNs.has_value() ||
            !fault.failureProbability.has_value() ||
            fault.warningLeadTimeNs.has_value() ||
            !fault.riskDurationNs.has_value() || fault.riskDurationNs.value() <= 0 ||
            fault.durationNs.has_value() ||
            fault.noticeTimeNs.value() >
                std::numeric_limits<int64_t>::max() - fault.riskDurationNs.value() ||
            fault.noticeTimeNs.value() + fault.riskDurationNs.value() != nowNs)
        {
            throw FaultControllerError(
                "generated NOTICE_CLEAR must close one current risk-only episode");
        }
        return;
    }

    if (!fault.faultOccurred || fault.startTimeNs != nowNs ||
        fault.riskDurationNs.has_value())
    {
        throw FaultControllerError("generated START fields do not match the current time");
    }
    if (fault.faultType == FaultType::SATELLITE)
    {
        if (fault.noticeTimeNs.has_value() || fault.failureProbability.has_value() ||
            fault.warningLeadTimeNs.has_value() || fault.durationNs.has_value())
        {
            throw FaultControllerError(
                "generated satellite START must be permanent and unannounced");
        }
        return;
    }

    if (!fault.failureProbability.has_value() || !fault.durationNs.has_value() ||
        fault.durationNs.value() <= 0 ||
        fault.startTimeNs.value() >
            std::numeric_limits<int64_t>::max() - fault.durationNs.value())
    {
        throw FaultControllerError(
            "generated compute START requires probability and positive duration");
    }
    if (fault.noticeTimeNs.has_value())
    {
        if (fault.noticeTimeNs.value() > nowNs ||
            fault.warningLeadTimeNs != nowNs - fault.noticeTimeNs.value())
        {
            throw FaultControllerError(
                "generated compute START warning lead time is inconsistent");
        }
    }
    else if (fault.warningLeadTimeNs.has_value())
    {
        throw FaultControllerError(
            "generated compute START cannot contain lead time without notice");
    }
    if (nowNs < 0 || nowNs >= simulationDurationNs)
    {
        throw FaultControllerError("generated START is outside the simulation window");
    }
}

} // namespace

TypeId
FaultController::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::FaultController")
                               .SetParent<Object>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<FaultController>();
    return typeId;
}

FaultController::FaultController() = default;

FaultController::~FaultController() = default;

const char*
FaultEventTypeToString(FaultEventType eventType)
{
    switch (eventType)
    {
    case FaultEventType::NOTICE:
        return "NOTICE";
    case FaultEventType::NOTICE_CLEAR:
        return "NOTICE_CLEAR";
    case FaultEventType::START:
        return "START";
    case FaultEventType::RECOVERY:
        return "RECOVERY";
    }
    return "UNKNOWN";
}

void
FaultController::Initialize(const std::vector<uint32_t>& satelliteIds,
                            int64_t simulationDurationNs)
{
    if (m_configured)
    {
        throw FaultControllerError("FaultController can only be configured once");
    }
    if (simulationDurationNs <= 0)
    {
        throw FaultControllerError("FaultController simulation duration must be positive");
    }
    if (!Simulator::Now().IsZero())
    {
        throw FaultControllerError(
            "FaultController must be configured at simulation time zero");
    }

    m_simulationDurationNs = simulationDurationNs;
    m_satelliteIds.insert(satelliteIds.begin(), satelliteIds.end());
    if (m_satelliteIds.size() != satelliteIds.size())
    {
        throw FaultControllerError("FaultController satellite IDs must be unique");
    }
    m_state.Initialize(satelliteIds);
    m_configured = true;
}

void
FaultController::ScheduleBatch(int64_t simulationTimeNs)
{
    if (m_batchEvents.contains(simulationTimeNs))
    {
        return;
    }
    const int64_t nowNs = Simulator::Now().GetNanoSeconds();
    NS_ABORT_MSG_IF(simulationTimeNs < nowNs,
                    "FaultController cannot schedule a batch in the past");
    m_batchEvents.emplace(
        simulationTimeNs,
        Simulator::Schedule(NanoSeconds(simulationTimeNs - nowNs),
                            &FaultController::ProcessBatch,
                            this,
                            simulationTimeNs));
}

void
FaultController::ScheduleRecovery(const FaultDefinition& fault)
{
    const std::optional<int64_t> recoveryTimeNs = fault.GetRecoveryTimeNs();
    if (recoveryTimeNs.has_value() && recoveryTimeNs.value() < m_simulationDurationNs)
    {
        m_batches[recoveryTimeNs.value()].push_back(
            {FaultEventType::RECOVERY, fault});
        ScheduleBatch(recoveryTimeNs.value());
    }
}

void
FaultController::Configure(const FaultTrace& trace,
                           const std::vector<uint32_t>& satelliteIds,
                           int64_t simulationDurationNs)
{
    Initialize(satelliteIds, simulationDurationNs);
    m_trace = trace;
    for (const FaultDefinition& fault : m_trace.faults)
    {
        if (fault.noticeTimeNs.has_value())
        {
            m_batches[fault.noticeTimeNs.value()].push_back(
                {FaultEventType::NOTICE,
                 MakeTimeGatedEventFault(FaultEventType::NOTICE, fault)});
        }
        if (!fault.faultOccurred)
        {
            const std::optional<int64_t> clearTimeNs = fault.GetRiskClearTimeNs();
            NS_ABORT_MSG_IF(!clearTimeNs.has_value(),
                            "risk-only fault has no notice clear time");
            if (clearTimeNs.value() < m_simulationDurationNs)
            {
                m_batches[clearTimeNs.value()].push_back(
                    {FaultEventType::NOTICE_CLEAR,
                     MakeTimeGatedEventFault(FaultEventType::NOTICE_CLEAR, fault)});
            }
            continue;
        }
        NS_ABORT_MSG_IF(!fault.startTimeNs.has_value(),
                        "occurred fault has no start time");
        m_batches[fault.startTimeNs.value()].push_back({FaultEventType::START, fault});
        ScheduleRecovery(fault);
    }

    for (const auto& [simulationTimeNs, events] : m_batches)
    {
        static_cast<void>(events);
        ScheduleBatch(simulationTimeNs);
    }
}

void
FaultController::ConfigureGeneration(const std::vector<uint32_t>& satelliteIds,
                                     int64_t simulationDurationNs)
{
    Initialize(satelliteIds, simulationDurationNs);
    m_generationMode = true;
    m_trace.schemaVersion = FAULT_TRACE_SCHEMA_VERSION;
}

void
FaultController::SubmitGeneratedBatch(const std::vector<GeneratedFaultEvent>& events)
{
    if (!m_configured || !m_generationMode)
    {
        throw FaultControllerError(
            "generated events require ConfigureGeneration");
    }
    if (events.empty())
    {
        throw FaultControllerError("generated event batch must not be empty");
    }
    const int64_t nowNs = Simulator::Now().GetNanoSeconds();
    if (nowNs < 0 || nowNs >= m_simulationDurationNs)
    {
        throw FaultControllerError("generated event batch is outside the simulation window");
    }
    if (m_processedBatchTimes.contains(nowNs))
    {
        throw FaultControllerError(
            "generated events for one timestamp must be submitted in one batch");
    }

    std::set<std::pair<FaultEventType, uint64_t>> batchKeys;
    for (const GeneratedFaultEvent& event : events)
    {
        ValidateGeneratedEvent(event,
                               nowNs,
                               m_simulationDurationNs,
                               m_satelliteIds);
        const auto key = std::make_pair(event.eventType, event.fault.faultId);
        if (!batchKeys.insert(key).second || m_generatedEventKeys.contains(key))
        {
            throw FaultControllerError("generated fault event was submitted twice");
        }
    }

    const auto scheduled = m_batchEvents.find(nowNs);
    if (scheduled != m_batchEvents.end())
    {
        if (scheduled->second.IsPending())
        {
            Simulator::Cancel(scheduled->second);
        }
        m_batchEvents.erase(scheduled);
    }
    for (const GeneratedFaultEvent& event : events)
    {
        m_generatedEventKeys.emplace(event.eventType, event.fault.faultId);
        m_batches[nowNs].push_back(
            {event.eventType,
             MakeTimeGatedEventFault(event.eventType, event.fault)});
        if (event.eventType == FaultEventType::START)
        {
            ScheduleRecovery(event.fault);
        }
    }
    ProcessBatch(nowNs);
}

void
FaultController::FinalizeGeneratedTrace(const FaultTrace& trace)
{
    if (!m_configured || !m_generationMode)
    {
        throw FaultControllerError(
            "generated trace finalization requires ConfigureGeneration");
    }
    if (m_generatedTraceFinalized)
    {
        throw FaultControllerError("generated trace can only be finalized once");
    }
    if (trace.schemaVersion != FAULT_TRACE_SCHEMA_VERSION)
    {
        throw FaultControllerError("generated trace must use schema version 2");
    }
    m_trace = trace;
    m_generatedTraceFinalized = true;
}

void
FaultController::BindTopology(SatelliteTopologyController& topology)
{
    if (!m_configured)
    {
        throw FaultControllerError(
            "FaultController must be configured before topology binding");
    }
    if (m_topology != nullptr)
    {
        throw FaultControllerError("FaultController topology binding can only occur once");
    }
    m_topology = &topology;
}

void
FaultController::BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator)
{
    if (!m_configured)
    {
        throw FaultControllerError(
            "FaultController must be configured before task binding");
    }
    if (taskCoordinator == nullptr)
    {
        throw FaultControllerError("FaultController task binding must not be null");
    }
    if (m_taskCoordinator != nullptr)
    {
        throw FaultControllerError("FaultController task binding can only occur once");
    }
    m_taskCoordinator = taskCoordinator;
}

void
FaultController::ProcessBatch(int64_t simulationTimeNs)
{
    NS_ABORT_MSG_IF(!m_configured || simulationTimeNs != Simulator::Now().GetNanoSeconds(),
                    "FaultController batch time is invalid");
    const auto batch = m_batches.find(simulationTimeNs);
    NS_ABORT_MSG_IF(batch == m_batches.end(),
                    "FaultController has no scheduled batch at the current time");
    NS_ABORT_MSG_IF(!m_processedBatchTimes.insert(simulationTimeNs).second,
                    "FaultController processed one timestamp twice");
    std::sort(batch->second.begin(),
              batch->second.end(),
              [](const ScheduledFaultEvent& left,
                 const ScheduledFaultEvent& right) {
                  return std::make_tuple(GetEventPriority(left.eventType),
                                         left.fault.faultId) <
                         std::make_tuple(GetEventPriority(right.eventType),
                                         right.fault.faultId);
              });

    std::vector<TaskFaultNodeChange> recoveredNodes;
    std::vector<TaskFaultNodeChange> startedNodes;
    std::map<uint32_t, std::size_t> startRecordIndexes;
    std::vector<std::size_t> topologyRecordIndexes;
    std::set<uint32_t> recoveredNodeSet;
    std::set<uint32_t> startedNodeSet;
    bool refreshNaturalState = false;
    for (const ScheduledFaultEvent& event : batch->second)
    {
        if (event.eventType == FaultEventType::RECOVERY)
        {
            m_state.RecoverFault(event.fault);
            NS_ABORT_MSG_IF(!recoveredNodeSet.insert(event.fault.nodeId).second,
                            "FaultController recovered one node twice in one batch");
            recoveredNodes.push_back(
                {event.fault.nodeId,
                 event.fault.faultType == FaultType::SATELLITE
                     ? TaskFaultKind::SATELLITE
                     : TaskFaultKind::COMPUTE});
            refreshNaturalState =
                refreshNaturalState || event.fault.faultType == FaultType::SATELLITE;
        }
        else if (event.eventType == FaultEventType::START)
        {
            m_state.StartFault(event.fault);
            NS_ABORT_MSG_IF(!startedNodeSet.insert(event.fault.nodeId).second,
                            "FaultController started one node twice in one batch");
            startedNodes.push_back(
                {event.fault.nodeId,
                 event.fault.faultType == FaultType::SATELLITE
                     ? TaskFaultKind::SATELLITE
                     : TaskFaultKind::COMPUTE});
        }

        const FaultNodeAvailability& availability =
            m_state.GetNodeAvailability(event.fault.nodeId);
        m_events.push_back({simulationTimeNs,
                            event.fault.faultId,
                            event.fault.nodeId,
                            event.fault.faultType,
                            event.eventType,
                            event.fault.noticeTimeNs,
                            event.fault.startTimeNs,
                            event.fault.durationNs,
                            event.fault.failureProbability,
                            event.fault.warningLeadTimeNs,
                            event.fault.riskDurationNs,
                            availability.satelliteAvailable,
                            availability.communicationAvailable,
                            availability.computeAvailable,
                            0,
                            0,
                            false});
        if (event.eventType == FaultEventType::START)
        {
            startRecordIndexes.emplace(event.fault.nodeId, m_events.size() - 1);
        }
        if (event.fault.faultType == FaultType::SATELLITE &&
            (event.eventType == FaultEventType::START ||
             event.eventType == FaultEventType::RECOVERY))
        {
            topologyRecordIndexes.push_back(m_events.size() - 1);
        }
    }

    if (!recoveredNodes.empty() || !startedNodes.empty())
    {
        const bool containsComputeChange =
            std::any_of(recoveredNodes.begin(),
                        recoveredNodes.end(),
                        [](const TaskFaultNodeChange& change) {
                            return change.kind == TaskFaultKind::COMPUTE;
                        }) ||
            std::any_of(startedNodes.begin(),
                        startedNodes.end(),
                        [](const TaskFaultNodeChange& change) {
                            return change.kind == TaskFaultKind::COMPUTE;
                        });
        NS_ABORT_MSG_IF(containsComputeChange && m_taskCoordinator == nullptr,
                        "compute fault execution requires a TaskCoordinator");
        if (m_taskCoordinator != nullptr)
        {
            const std::map<uint32_t, TaskFaultImpact> impacts =
                m_taskCoordinator->ApplyFaultBatch(recoveredNodes, startedNodes);
            for (const auto& [nodeId, impact] : impacts)
            {
                const auto record = startRecordIndexes.find(nodeId);
                NS_ABORT_MSG_IF(record == startRecordIndexes.end(),
                                "fault impact has no matching START record");
                m_events[record->second].affectedTaskCount = impact.affectedTaskCount;
                m_events[record->second].affectedTransferCount =
                    impact.affectedTransferCount;
            }
        }
    }

    if (!topologyRecordIndexes.empty())
    {
        NS_ABORT_MSG_IF(m_topology == nullptr,
                        "satellite fault execution requires a topology controller");
        const bool routeRecomputed = m_topology->ApplyCommunicationFaultOverlay(
            m_state.GetCommunicationUnavailableNodeIds(),
            refreshNaturalState);
        if (routeRecomputed)
        {
            m_events[topologyRecordIndexes.back()].routeRecomputed = true;
        }
    }
}

const FaultTrace&
FaultController::GetTrace() const
{
    if (!m_configured)
    {
        throw FaultControllerError("FaultController is not configured");
    }
    return m_trace;
}

const FaultState&
FaultController::GetState() const
{
    if (!m_configured)
    {
        throw FaultControllerError("FaultController is not configured");
    }
    return m_state;
}

const std::vector<FaultRuntimeEventRecord>&
FaultController::GetEvents() const
{
    return m_events;
}

void
FaultController::DoDispose()
{
    for (auto& [simulationTimeNs, event] : m_batchEvents)
    {
        static_cast<void>(simulationTimeNs);
        if (event.IsPending())
        {
            Simulator::Cancel(event);
        }
    }
    m_topology = nullptr;
    m_taskCoordinator = nullptr;
    Object::DoDispose();
}

} // namespace ns3
