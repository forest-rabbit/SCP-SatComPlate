/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Apply all fault events at one timestamp as one deterministic transaction.

#include "fault-controller.h"

#include "../task/task-coordinator.h"

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
    for (const FaultDefinition& fault : trace.faults)
    {
        if (fault.faultType == FaultType::SATELLITE)
        {
            throw FaultControllerError(
                "satellite fault execution is reserved for N4A PR 5");
        }
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

    std::vector<uint32_t> recoveredComputeNodes;
    std::vector<uint32_t> startedComputeNodes;
    std::map<uint32_t, std::size_t> startRecordIndexes;
    std::set<uint32_t> recoveredNodeSet;
    std::set<uint32_t> startedNodeSet;
    for (const ScheduledFaultEvent& event : batch->second)
    {
        if (event.eventType == FaultEventType::RECOVERY)
        {
            m_state.RecoverFault(event.fault);
            NS_ABORT_MSG_IF(!recoveredNodeSet.insert(event.fault.nodeId).second,
                            "FaultController recovered one node twice in one batch");
            recoveredComputeNodes.push_back(event.fault.nodeId);
        }
        else if (event.eventType == FaultEventType::START)
        {
            m_state.StartFault(event.fault);
            NS_ABORT_MSG_IF(!startedNodeSet.insert(event.fault.nodeId).second,
                            "FaultController started one node twice in one batch");
            startedComputeNodes.push_back(event.fault.nodeId);
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
    }

    if (!recoveredComputeNodes.empty() || !startedComputeNodes.empty())
    {
        NS_ABORT_MSG_IF(m_taskCoordinator == nullptr,
                        "compute fault execution requires a TaskCoordinator");
        const std::map<uint32_t, TaskFaultImpact> impacts =
            m_taskCoordinator->ApplyComputeFaultBatch(recoveredComputeNodes,
                                                      startedComputeNodes);
        for (const auto& [nodeId, impact] : impacts)
        {
            const auto record = startRecordIndexes.find(nodeId);
            NS_ABORT_MSG_IF(record == startRecordIndexes.end(),
                            "compute fault impact has no matching START record");
            m_events[record->second].affectedTaskCount = impact.affectedTaskCount;
            m_events[record->second].affectedTransferCount =
                impact.affectedTransferCount;
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
    m_taskCoordinator = nullptr;
    Object::DoDispose();
}

} // namespace ns3
