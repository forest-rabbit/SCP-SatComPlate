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
    case FaultEventType::RECOVERY:
        return 1;
    case FaultEventType::START:
        return 2;
    }
    return 3;
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
    case FaultEventType::START:
        return "START";
    case FaultEventType::RECOVERY:
        return "RECOVERY";
    }
    return "UNKNOWN";
}

void
FaultController::Configure(const FaultTrace& trace,
                           const std::vector<uint32_t>& satelliteIds,
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

    m_trace = trace;
    m_simulationDurationNs = simulationDurationNs;
    m_state.Initialize(satelliteIds);
    for (const FaultDefinition& fault : m_trace.faults)
    {
        if (fault.noticeTimeNs.has_value())
        {
            m_batches[fault.noticeTimeNs.value()].push_back(
                {FaultEventType::NOTICE, fault});
        }
        m_batches[fault.startTimeNs].push_back({FaultEventType::START, fault});
        const std::optional<int64_t> recoveryTimeNs = fault.GetRecoveryTimeNs();
        if (recoveryTimeNs.has_value() &&
            recoveryTimeNs.value() < m_simulationDurationNs)
        {
            m_batches[recoveryTimeNs.value()].push_back(
                {FaultEventType::RECOVERY, fault});
        }
    }

    m_batchEvents.reserve(m_batches.size());
    for (auto& [simulationTimeNs, events] : m_batches)
    {
        std::sort(events.begin(),
                  events.end(),
                  [](const ScheduledFaultEvent& left,
                     const ScheduledFaultEvent& right) {
                      return std::make_tuple(GetEventPriority(left.eventType),
                                             left.fault.faultId) <
                             std::make_tuple(GetEventPriority(right.eventType),
                                             right.fault.faultId);
                  });
        m_batchEvents.push_back(
            Simulator::Schedule(NanoSeconds(simulationTimeNs),
                                &FaultController::ProcessBatch,
                                this,
                                simulationTimeNs));
    }
    m_configured = true;
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
            event.eventType != FaultEventType::NOTICE)
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
    for (EventId& event : m_batchEvents)
    {
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
