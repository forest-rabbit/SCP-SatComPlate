/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-model-engine.h"

#include "fault-parameter-validator.h"

#include "../common/time-conversion.h"
#include "../task/compute-service.h"
#include "../task/task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(FaultModelEngine);

namespace
{

constexpr int64_t COMPUTE_FAULT_STREAM_BASE = 1000000;

} // namespace

TypeId
FaultModelEngine::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::FaultModelEngine")
                               .SetParent<Object>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<FaultModelEngine>();
    return typeId;
}

FaultModelEngine::FaultModelEngine() = default;

FaultModelEngine::~FaultModelEngine() = default;

void
FaultModelEngine::Configure(const FaultParameters& parameters,
                            const std::vector<uint32_t>& computeNodeIds,
                            int64_t simulationDurationNs,
                            Ptr<FaultController> faultController)
{
    if (m_configured || !Simulator::Now().IsZero())
    {
        throw FaultModelEngineError(
            "FaultModelEngine must be configured once at time zero");
    }
    if (simulationDurationNs <= 0 || faultController == nullptr)
    {
        throw FaultModelEngineError(
            "FaultModelEngine requires duration and FaultController");
    }
    ValidateFaultParameters(parameters);
    if (parameters.f2.enabled || parameters.f3.enabled)
    {
        throw FaultModelEngineError(
            "F2/F3 cannot be enabled before their N4B increments");
    }
    if (parameters.f1.enabled && computeNodeIds.empty())
    {
        throw FaultModelEngineError(
            "enabled F1 requires at least one compute node");
    }

    std::set<uint32_t> uniqueNodeIds(computeNodeIds.begin(), computeNodeIds.end());
    if (uniqueNodeIds.size() != computeNodeIds.size())
    {
        throw FaultModelEngineError("F1 compute node IDs must be unique");
    }
    m_parameters = parameters;
    m_checkIntervalNs = SatComputeSecondsToNanoseconds(
        parameters.checkIntervalSeconds,
        "fault.checkIntervalSeconds");
    m_recoveryDurationNs = SatComputeSecondsToNanoseconds(
        parameters.recoverableComputeDurationSeconds,
        "fault.recoverableComputeDurationSeconds");
    if (m_checkIntervalNs <= 0 || m_recoveryDurationNs <= 0)
    {
        throw FaultModelEngineError("fault intervals must convert to positive nanoseconds");
    }
    m_simulationDurationNs = simulationDurationNs;
    m_faultController = faultController;
    m_trace.schemaVersion = FAULT_TRACE_SCHEMA_VERSION;
    if (parameters.f1.enabled)
    {
        m_selfStateModel.emplace(parameters.f1);
        for (const uint32_t nodeId : uniqueNodeIds)
        {
            NodeState state;
            state.selfState = m_selfStateModel->CreateInitialSnapshot();
            state.random = CreateObject<UniformRandomVariable>();
            state.random->SetStream(COMPUTE_FAULT_STREAM_BASE + nodeId);
            m_nodes.emplace(nodeId, state);
        }
    }

    const int64_t intervalNs = m_checkIntervalNs;
    for (int64_t timeNs = intervalNs; timeNs < simulationDurationNs;)
    {
        m_tickEvents.push_back(
            Simulator::Schedule(NanoSeconds(timeNs),
                                &FaultModelEngine::Tick,
                                this,
                                timeNs));
        if (timeNs > std::numeric_limits<int64_t>::max() - intervalNs)
        {
            break;
        }
        timeNs += intervalNs;
    }
    m_configured = true;
}

void
FaultModelEngine::BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator)
{
    if (!m_configured || m_bound)
    {
        throw FaultModelEngineError(
            "FaultModelEngine task binding is invalid");
    }
    if (m_parameters.f1.enabled && taskCoordinator == nullptr)
    {
        throw FaultModelEngineError("enabled F1 requires a TaskCoordinator");
    }
    m_taskCoordinator = taskCoordinator;
    if (taskCoordinator != nullptr)
    {
        for (const Ptr<ComputeService>& service : taskCoordinator->GetComputeServices())
        {
            const auto node = m_nodes.find(service->GetNodeId());
            if (node != m_nodes.end())
            {
                node->second.computeService = service;
            }
        }
    }
    for (const auto& [nodeId, state] : m_nodes)
    {
        if (state.computeService == nullptr)
        {
            throw FaultModelEngineError(
                "F1 has no ComputeService for node_id=" + std::to_string(nodeId));
        }
    }
    m_bound = true;
}

FaultDefinition
FaultModelEngine::MakeNotice(uint32_t nodeId,
                                   const RiskEpisode& episode) const
{
    FaultDefinition notice;
    notice.faultId = episode.faultId;
    notice.nodeId = nodeId;
    notice.faultType = FaultType::COMPUTE;
    notice.faultOccurred = false;
    notice.noticeTimeNs = episode.noticeTimeNs;
    notice.failureProbability = episode.noticeProbability;
    return notice;
}

FaultDefinition
FaultModelEngine::MakeRiskOnly(uint32_t nodeId,
                                     const RiskEpisode& episode,
                                     int64_t clearTimeNs) const
{
    FaultDefinition riskOnly = MakeNotice(nodeId, episode);
    riskOnly.riskDurationNs = clearTimeNs - episode.noticeTimeNs;
    return riskOnly;
}

FaultDefinition
FaultModelEngine::MakeComputeFault(
    uint32_t nodeId,
    uint64_t faultId,
    const std::optional<RiskEpisode>& episode,
    double currentProbability,
    int64_t startTimeNs) const
{
    FaultDefinition fault;
    fault.faultId = faultId;
    fault.nodeId = nodeId;
    fault.faultType = FaultType::COMPUTE;
    fault.faultOccurred = true;
    fault.startTimeNs = startTimeNs;
    fault.failureProbability = currentProbability;
    fault.durationNs = m_recoveryDurationNs;
    if (episode.has_value())
    {
        fault.noticeTimeNs = episode->noticeTimeNs;
        fault.failureProbability = episode->noticeProbability;
        fault.warningLeadTimeNs = startTimeNs - episode->noticeTimeNs;
    }
    return fault;
}

void
FaultModelEngine::Tick(int64_t simulationTimeNs)
{
    NS_ABORT_MSG_IF(!m_configured || !m_bound || m_finalized ||
                        simulationTimeNs != Simulator::Now().GetNanoSeconds(),
                    "FaultModelEngine tick state is invalid");
    const double intervalSeconds =
        m_parameters.checkIntervalSeconds;
    std::vector<GeneratedFaultEvent> events;
    std::vector<FaultDefinition> completedRecords;
    for (auto& [nodeId, state] : m_nodes)
    {
        const bool computeAvailable =
            m_faultController->GetState().IsComputeAvailable(nodeId);
        const bool busy = computeAvailable && state.computeService->IsComputeAvailable() &&
                          state.computeService->HasRunningTask();
        m_selfStateModel->Update(state.selfState, busy, intervalSeconds);
        if (!computeAvailable)
        {
            continue;
        }

        const bool riskActive = m_selfStateModel->IsRiskActive(state.selfState);
        if (riskActive && !state.riskEpisode.has_value())
        {
            NS_ABORT_MSG_IF(m_nextFaultId == std::numeric_limits<uint64_t>::max(),
                            "generated fault ID overflow");
            state.riskEpisode =
                RiskEpisode{m_nextFaultId++,
                            simulationTimeNs,
                            state.selfState.stepFailureProbability};
            events.push_back(
                {FaultEventType::NOTICE, MakeNotice(nodeId, state.riskEpisode.value())});
        }
        else if (!riskActive && state.riskEpisode.has_value())
        {
            FaultDefinition riskOnly =
                MakeRiskOnly(nodeId, state.riskEpisode.value(), simulationTimeNs);
            events.push_back({FaultEventType::NOTICE_CLEAR, riskOnly});
            completedRecords.push_back(riskOnly);
            state.riskEpisode = std::nullopt;
        }

        const double randomValue = state.random->GetValue();
        if (randomValue < state.selfState.stepFailureProbability)
        {
            uint64_t faultId;
            if (state.riskEpisode.has_value())
            {
                faultId = state.riskEpisode->faultId;
            }
            else
            {
                NS_ABORT_MSG_IF(m_nextFaultId == std::numeric_limits<uint64_t>::max(),
                                "generated fault ID overflow");
                faultId = m_nextFaultId++;
            }
            FaultDefinition fault =
                MakeComputeFault(nodeId,
                                 faultId,
                                 state.riskEpisode,
                                 state.selfState.stepFailureProbability,
                                 simulationTimeNs);
            events.push_back({FaultEventType::START, fault});
            completedRecords.push_back(fault);
            state.riskEpisode = std::nullopt;
        }
    }

    if (!events.empty())
    {
        m_faultController->SubmitGeneratedBatch(events);
        m_trace.faults.insert(m_trace.faults.end(),
                              completedRecords.begin(),
                              completedRecords.end());
    }
}

const FaultTrace&
FaultModelEngine::Finalize()
{
    if (!m_configured || !m_bound || m_finalized)
    {
        throw FaultModelEngineError("FaultModelEngine finalization is invalid");
    }
    for (auto& [nodeId, state] : m_nodes)
    {
        if (state.riskEpisode.has_value())
        {
            m_trace.faults.push_back(
                MakeRiskOnly(nodeId,
                             state.riskEpisode.value(),
                             m_simulationDurationNs));
            state.riskEpisode = std::nullopt;
        }
    }
    m_faultController->FinalizeGeneratedTrace(m_trace);
    m_finalized = true;
    return m_trace;
}

std::vector<FaultModelNodeSnapshot>
FaultModelEngine::GetNodeSnapshots() const
{
    if (!m_configured)
    {
        throw FaultModelEngineError("FaultModelEngine is not configured");
    }
    std::vector<FaultModelNodeSnapshot> snapshots;
    snapshots.reserve(m_nodes.size());
    for (const auto& [nodeId, state] : m_nodes)
    {
        snapshots.push_back(
            {nodeId,
             state.selfState,
             state.riskEpisode.has_value(),
             m_faultController->GetState().IsComputeAvailable(nodeId)});
    }
    return snapshots;
}

void
FaultModelEngine::DoDispose()
{
    for (EventId& event : m_tickEvents)
    {
        if (event.IsPending())
        {
            Simulator::Cancel(event);
        }
    }
    for (auto& [nodeId, state] : m_nodes)
    {
        static_cast<void>(nodeId);
        state.random = nullptr;
        state.computeService = nullptr;
    }
    m_faultController = nullptr;
    m_taskCoordinator = nullptr;
    Object::DoDispose();
}

} // namespace ns3
