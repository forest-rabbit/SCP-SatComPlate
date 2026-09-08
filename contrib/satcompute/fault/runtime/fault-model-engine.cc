/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-model-engine.h"

#include "ns3/compute-failure-predictor.h"
#include "ns3/fault-parameter-validator.h"

#include "../../common/time-conversion.h"
#include "../../task/compute-service.h"
#include "../../task/task-coordinator.h"
#include "../../topology/orbit/online-orbit-constellation.h"

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

constexpr int64_t F1_COMPUTE_FAULT_STREAM_BASE = 1000000;
constexpr int64_t F2_COMPUTE_FAULT_STREAM_BASE = 2000000;
constexpr int64_t F3_EVENT_TIME_STREAM = 3000000;
constexpr int64_t F3_NODE_SELECTION_STREAM = 3000001;

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
                            const std::vector<uint32_t>& satelliteIds,
                            const std::vector<uint32_t>& computeNodeIds,
                            int64_t simulationDurationNs,
                            Ptr<FaultController> faultController,
                            bool probabilityAuditEnabled)
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
    if (!parameters.f1.enabled && !parameters.f2.enabled && !parameters.f3.enabled)
    {
        throw FaultModelEngineError(
            "FaultModelEngine requires one supported enabled fault source");
    }
    if ((parameters.f1.enabled || parameters.f2.enabled) && computeNodeIds.empty())
    {
        throw FaultModelEngineError(
            "enabled F1/F2 requires at least one compute node");
    }

    std::set<uint32_t> uniqueSatelliteIds(satelliteIds.begin(), satelliteIds.end());
    if (uniqueSatelliteIds.empty() || uniqueSatelliteIds.size() != satelliteIds.size())
    {
        throw FaultModelEngineError(
            "fault-model satellite IDs must be non-empty and unique");
    }
    std::set<uint32_t> uniqueNodeIds(computeNodeIds.begin(), computeNodeIds.end());
    if (uniqueNodeIds.size() != computeNodeIds.size())
    {
        throw FaultModelEngineError("fault-model compute node IDs must be unique");
    }
    if (!std::includes(uniqueSatelliteIds.begin(),
                       uniqueSatelliteIds.end(),
                       uniqueNodeIds.begin(),
                       uniqueNodeIds.end()))
    {
        throw FaultModelEngineError(
            "fault-model compute nodes must belong to the constellation");
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
    m_probabilityAuditEnabled = probabilityAuditEnabled;
    m_trace.schemaVersion = FAULT_TRACE_SCHEMA_VERSION;
    if (parameters.f1.enabled)
    {
        m_f1Model.emplace(parameters.f1);
    }
    if (parameters.f2.enabled)
    {
        m_f2Model.emplace(parameters.f2);
    }
    if (parameters.f3.enabled)
    {
        m_f3Model.emplace(parameters.f3);
    }
    for (const uint32_t nodeId : uniqueNodeIds)
    {
        NodeState state;
        if (m_f1Model.has_value())
        {
            state.f1State = m_f1Model->CreateInitialSnapshot();
        }
        if (m_f2Model.has_value())
        {
            state.f2State = m_f2Model->CreateInitialSnapshot();
        }
        if (m_f1Model.has_value())
        {
            state.f1Random = CreateObject<UniformRandomVariable>();
            state.f1Random->SetStream(F1_COMPUTE_FAULT_STREAM_BASE + nodeId);
        }
        if (m_f2Model.has_value())
        {
            state.f2Random = CreateObject<UniformRandomVariable>();
            state.f2Random->SetStream(F2_COMPUTE_FAULT_STREAM_BASE + nodeId);
        }
        m_nodes.emplace(nodeId, state);
    }

    std::map<int64_t, bool> processTimes;
    if (m_f1Model.has_value() || m_f2Model.has_value())
    {
        const int64_t intervalNs = m_checkIntervalNs;
        for (int64_t timeNs = intervalNs; timeNs < simulationDurationNs;)
        {
            processTimes[timeNs] = true;
            if (timeNs > std::numeric_limits<int64_t>::max() - intervalNs)
            {
                break;
            }
            timeNs += intervalNs;
        }
    }
    if (m_f3Model.has_value())
    {
        const std::vector<uint32_t> canonicalSatelliteIds(uniqueSatelliteIds.begin(),
                                                          uniqueSatelliteIds.end());
        const std::vector<F3DebrisFaultEvent> f3Schedule =
            m_f3Model->GenerateSchedule(canonicalSatelliteIds,
                                        simulationDurationNs,
                                        F3_EVENT_TIME_STREAM,
                                        F3_NODE_SELECTION_STREAM);
        for (const F3DebrisFaultEvent& event : f3Schedule)
        {
            m_f3EventsByTime[event.startTimeNs].push_back(event.nodeId);
            processTimes.try_emplace(event.startTimeNs, false);
        }
    }
    for (const auto& [timeNs, updateComputeModels] : processTimes)
    {
        m_modelEvents.push_back(
            Simulator::Schedule(NanoSeconds(timeNs),
                                &FaultModelEngine::ProcessTime,
                                this,
                                timeNs,
                                updateComputeModels));
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
    if ((m_parameters.f1.enabled || m_parameters.f2.enabled) &&
        taskCoordinator == nullptr)
    {
        throw FaultModelEngineError("enabled F1/F2 requires a TaskCoordinator");
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
                "fault model has no ComputeService for node_id=" +
                std::to_string(nodeId));
        }
    }
    m_bound = true;
}

void
FaultModelEngine::BindOrbitConstellation(
    const OnlineOrbitConstellation& constellation)
{
    if (!m_configured || !m_parameters.f2.enabled || m_constellation != nullptr)
    {
        throw FaultModelEngineError(
            "FaultModelEngine orbit binding is invalid");
    }
    for (auto& [nodeId, state] : m_nodes)
    {
        if (!constellation.GetIdMap().HasSatelliteId(nodeId))
        {
            throw FaultModelEngineError(
                "F2 compute node is absent from the orbit constellation: " +
                std::to_string(nodeId));
        }
        m_f2Model->Update(state.f2State,
                          constellation.GetPosition(nodeId),
                          0.0);
    }
    m_constellation = &constellation;
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

FaultDefinition
FaultModelEngine::MakeSatelliteFault(uint32_t nodeId,
                                     uint64_t faultId,
                                     int64_t startTimeNs) const
{
    FaultDefinition fault;
    fault.faultId = faultId;
    fault.nodeId = nodeId;
    fault.faultType = FaultType::SATELLITE;
    fault.faultOccurred = true;
    fault.startTimeNs = startTimeNs;
    return fault;
}

void
FaultModelEngine::ShortenActiveComputeFault(NodeState& state,
                                            int64_t simulationTimeNs)
{
    if (!state.activeComputeFault.has_value())
    {
        return;
    }
    const FaultDefinition original = state.activeComputeFault.value();
    const int64_t originalRecoveryTimeNs = original.GetRecoveryTimeNs().value();
    if (originalRecoveryTimeNs <= simulationTimeNs)
    {
        state.activeComputeFault = std::nullopt;
        return;
    }
    NS_ABORT_MSG_IF(!original.startTimeNs.has_value() ||
                        original.startTimeNs.value() >= simulationTimeNs,
                    "F3 cannot shorten a non-positive compute-fault interval");
    FaultDefinition shortened = original;
    shortened.durationNs = simulationTimeNs - original.startTimeNs.value();
    const auto traceRecord = std::find_if(
        m_trace.faults.begin(),
        m_trace.faults.end(),
        [&original](const FaultDefinition& fault) {
            return fault.faultId == original.faultId;
        });
    NS_ABORT_MSG_IF(traceRecord == m_trace.faults.end() ||
                        traceRecord->faultType != FaultType::COMPUTE ||
                        !traceRecord->faultOccurred,
                    "active compute fault has no generated trace record");
    traceRecord->durationNs = shortened.durationNs;
    m_faultController->ShortenGeneratedComputeFault(shortened,
                                                    originalRecoveryTimeNs);
    state.activeComputeFault = std::nullopt;
}

void
FaultModelEngine::RecordProbability(uint32_t nodeId,
                                    const NodeState& state,
                                    int64_t simulationTimeNs)
{
    if (!m_probabilityAuditEnabled || !state.riskEpisode.has_value() ||
        !state.computeService->IsComputeAvailable())
    {
        return;
    }
    const std::optional<RunningComputeTaskSnapshot> task =
        state.computeService->GetRunningTaskSnapshot();
    if (!task.has_value())
    {
        return;
    }

    ComputeFailurePredictionInput input;
    input.f1Model = m_f1Model.has_value() ? &m_f1Model.value() : nullptr;
    input.f1State = state.f1State;
    input.f2Model = m_f2Model.has_value() ? &m_f2Model.value() : nullptr;
    input.f2State = state.f2State;
    if (m_f2Model.has_value())
    {
        input.f2PositionAtTime =
            [this, nodeId](int64_t targetTimeNs) {
                return m_constellation->GetPositionAt(nodeId,
                                                      NanoSeconds(targetTimeNs));
            };
    }
    input.predictionTimeNs = simulationTimeNs;
    input.remainingComputeTimeNs = task->remainingTimeNs;
    input.checkIntervalNs = m_checkIntervalNs;
    const ComputeFailurePrediction prediction =
        PredictComputeFailureBeforeFinish(input);
    const RiskEpisode& risk = state.riskEpisode.value();
    m_probabilityRecords.push_back(
        {simulationTimeNs,
         risk.faultId,
         nodeId,
         task->taskId,
         risk.noticeTimeNs,
         simulationTimeNs - risk.noticeTimeNs,
         task->startTimeNs,
         task->serviceTimeNs,
         task->elapsedTimeNs,
         task->remainingTimeNs,
         simulationTimeNs + task->remainingTimeNs,
         task->completionRatio,
         prediction.f1StepFailureProbability,
         prediction.f2StepFailureProbability,
         prediction.combinedStepFailureProbability,
         prediction.horizonStepCount,
         prediction.predictedFailureProbability});
}

void
FaultModelEngine::ProcessTime(int64_t simulationTimeNs,
                              bool updateComputeModels)
{
    NS_ABORT_MSG_IF(!m_configured || !m_bound || m_finalized ||
                        (m_parameters.f2.enabled && m_constellation == nullptr) ||
                        simulationTimeNs != Simulator::Now().GetNanoSeconds(),
                    "FaultModelEngine scheduled state is invalid");
    std::set<uint32_t> f3NodeIds;
    const auto f3Events = m_f3EventsByTime.find(simulationTimeNs);
    if (f3Events != m_f3EventsByTime.end())
    {
        f3NodeIds.insert(f3Events->second.begin(), f3Events->second.end());
        NS_ABORT_MSG_IF(f3NodeIds.size() != f3Events->second.size(),
                        "F3 selected one node twice at one timestamp");
    }

    std::vector<GeneratedFaultEvent> events;
    std::vector<FaultDefinition> completedRecords;
    for (auto& [nodeId, state] : m_nodes)
    {
        if (state.activeComputeFault.has_value() &&
            state.activeComputeFault->GetRecoveryTimeNs().value() <= simulationTimeNs)
        {
            state.activeComputeFault = std::nullopt;
        }
        if (!updateComputeModels ||
            !m_faultController->GetState().IsSatelliteAvailable(nodeId))
        {
            continue;
        }
        const bool computeAvailable =
            m_faultController->GetState().IsComputeAvailable(nodeId);
        if (m_f1Model.has_value())
        {
            const bool busy =
                computeAvailable && state.computeService->IsComputeAvailable() &&
                state.computeService->HasRunningTask();
            m_f1Model->Update(state.f1State,
                              busy,
                              m_parameters.checkIntervalSeconds);
        }
        if (m_f2Model.has_value())
        {
            m_f2Model->Update(state.f2State,
                              m_constellation->GetPosition(nodeId),
                              m_parameters.checkIntervalSeconds);
        }
        state.modelTimeNs = simulationTimeNs;
        if (f3NodeIds.contains(nodeId) || !computeAvailable)
        {
            continue;
        }

        const bool f1RiskActive =
            m_f1Model.has_value() && m_f1Model->IsRiskActive(state.f1State);
        const bool f2RiskActive =
            m_f2Model.has_value() && m_f2Model->IsRiskActive(state.f2State);
        const bool riskActive = f1RiskActive || f2RiskActive;
        const double f1StepFailureProbability =
            m_f1Model.has_value() ? state.f1State.stepFailureProbability : 0.0;
        const double f2StepFailureProbability =
            m_f2Model.has_value() ? state.f2State.stepFailureProbability : 0.0;
        const double stepFailureProbability =
            CombineComputeFaultProbabilities(f1StepFailureProbability,
                                             f2StepFailureProbability);
        if (riskActive && !state.riskEpisode.has_value())
        {
            NS_ABORT_MSG_IF(m_nextFaultId == std::numeric_limits<uint64_t>::max(),
                            "generated fault ID overflow");
            state.riskEpisode =
                RiskEpisode{m_nextFaultId++,
                            simulationTimeNs,
                            stepFailureProbability};
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

        RecordProbability(nodeId, state, simulationTimeNs);

        double f1RandomValue = 0.0;
        double f2RandomValue = 0.0;
        if (m_f1Model.has_value())
        {
            f1RandomValue = state.f1Random->GetValue();
            ++state.f1SampleCount;
        }
        if (m_f2Model.has_value())
        {
            f2RandomValue = state.f2Random->GetValue();
            ++state.f2SampleCount;
        }
        const ComputeFaultSourceOutcome outcome =
            EvaluateComputeFaultSources(f1StepFailureProbability,
                                        f1RandomValue,
                                        f2StepFailureProbability,
                                        f2RandomValue);
        if (outcome.f1Occurred)
        {
            ++state.f1OccurrenceCount;
        }
        if (outcome.f2Occurred)
        {
            ++state.f2OccurrenceCount;
        }
        if (outcome.computeFaultOccurred)
        {
            ++state.computeFaultCount;
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
                                 stepFailureProbability,
                                 simulationTimeNs);
            fault.f1Occurred = outcome.f1Occurred;
            fault.f2Occurred = outcome.f2Occurred;
            events.push_back({FaultEventType::START, fault});
            completedRecords.push_back(fault);
            state.activeComputeFault = fault;
            state.riskEpisode = std::nullopt;
        }
    }

    for (const uint32_t nodeId : f3NodeIds)
    {
        NS_ABORT_MSG_IF(!m_faultController->GetState().IsSatelliteAvailable(nodeId),
                        "F3 selected an already permanently failed node");
        const auto node = m_nodes.find(nodeId);
        if (node != m_nodes.end())
        {
            ShortenActiveComputeFault(node->second, simulationTimeNs);
            if (node->second.riskEpisode.has_value())
            {
                const RiskEpisode episode = node->second.riskEpisode.value();
                NS_ABORT_MSG_IF(episode.noticeTimeNs >= simulationTimeNs,
                                "F3 cannot close a zero-duration risk episode");
                FaultDefinition riskOnly =
                    MakeRiskOnly(nodeId, episode, simulationTimeNs);
                events.push_back({FaultEventType::NOTICE_CLEAR, riskOnly});
                completedRecords.push_back(riskOnly);
                node->second.riskEpisode = std::nullopt;
            }
        }
        NS_ABORT_MSG_IF(m_nextFaultId == std::numeric_limits<uint64_t>::max(),
                        "generated fault ID overflow");
        FaultDefinition fault =
            MakeSatelliteFault(nodeId, m_nextFaultId++, simulationTimeNs);
        events.push_back({FaultEventType::START, fault});
        completedRecords.push_back(fault);
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
    if (!m_configured || !m_bound || m_finalized ||
        (m_parameters.f2.enabled && m_constellation == nullptr))
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
             state.f1State,
             state.f2State,
             CombineComputeFaultProbabilities(
                 m_f1Model.has_value() ? state.f1State.stepFailureProbability : 0.0,
                 m_f2Model.has_value() ? state.f2State.stepFailureProbability : 0.0),
             state.f1SampleCount,
             state.f2SampleCount,
             state.f1OccurrenceCount,
             state.f2OccurrenceCount,
             state.computeFaultCount,
             state.riskEpisode.has_value(),
             m_faultController->GetState().IsComputeAvailable(nodeId)});
    }
    return snapshots;
}

ComputeRiskSnapshot
FaultModelEngine::QueryComputeRisk(uint32_t nodeId, int64_t horizonNs) const
{
    ComputeRiskSnapshot result;
    result.nodeId = nodeId;
    result.asOfTimeNs = Simulator::Now().GetNanoSeconds();
    result.horizonNs = horizonNs;
    if (horizonNs <= 0 || result.asOfTimeNs > std::numeric_limits<int64_t>::max() - horizonNs)
    {
        throw FaultModelEngineError("risk horizon must be positive and not overflow time");
    }
    const auto node = m_nodes.find(nodeId);
    if (!m_configured || !m_bound || m_finalized || m_faultController == nullptr ||
        result.asOfTimeNs >= m_simulationDurationNs || node == m_nodes.end() ||
        node->second.computeService == nullptr || (m_f2Model && m_constellation == nullptr))
    {
        return result;
    }
    const auto& state = node->second;
    result.permanentlyUnavailable = !m_faultController->GetState().IsSatelliteAvailable(nodeId);
    if (result.permanentlyUnavailable ||
        !m_faultController->GetState().IsComputeAvailable(nodeId) ||
        !state.computeService->IsComputeAvailable())
    {
        result.status = ComputeRiskStatus::UNAVAILABLE;
        return result;
    }
    result.status = ComputeRiskStatus::AVAILABLE;
    if (!m_f1Model && !m_f2Model)
    {
        result.pF1 = result.pF2 = result.pCompute = 0.0;
        return result;
    }
    auto f1 = state.f1State;
    auto f2 = state.f2State;
    const bool busy = state.computeService->HasRunningTask();
    const int64_t endNs = result.asOfTimeNs + horizonNs;
    double p1 = 0.0;
    double p2 = 0.0;
    // Project copies only. Catch up a pending current-time check without counting
    // it in the future interval; never inspect queued tasks or future F3 events.
    for (int64_t timeNs = state.modelTimeNs; timeNs <= endNs - m_checkIntervalNs;)
    {
        timeNs += m_checkIntervalNs;
        if (m_f1Model)
        {
            m_f1Model->Update(f1, busy, m_parameters.checkIntervalSeconds);
        }
        if (m_f2Model)
        {
            m_f2Model->Update(f2,
                              m_constellation->GetPositionAt(nodeId, NanoSeconds(timeNs)),
                              m_parameters.checkIntervalSeconds);
        }
        if (timeNs > result.asOfTimeNs)
        {
            ++result.checkCount;
            p1 = CombineComputeFaultProbabilities(p1, m_f1Model ? f1.stepFailureProbability : 0.0);
            p2 = CombineComputeFaultProbabilities(p2, m_f2Model ? f2.stepFailureProbability : 0.0);
        }
    }
    result.pF1 = p1;
    result.pF2 = p2;
    result.pCompute = CombineComputeFaultProbabilities(p1, p2);
    return result;
}

const std::vector<ComputeFailureProbabilityRecord>&
FaultModelEngine::GetProbabilityRecords() const
{
    if (!m_configured)
    {
        throw FaultModelEngineError("FaultModelEngine is not configured");
    }
    return m_probabilityRecords;
}

void
FaultModelEngine::DoDispose()
{
    for (EventId& event : m_modelEvents)
    {
        if (event.IsPending())
        {
            Simulator::Cancel(event);
        }
    }
    for (auto& [nodeId, state] : m_nodes)
    {
        static_cast<void>(nodeId);
        state.f1Random = nullptr;
        state.f2Random = nullptr;
        state.computeService = nullptr;
    }
    m_faultController = nullptr;
    m_taskCoordinator = nullptr;
    m_constellation = nullptr;
    m_probabilityRecords.clear();
    Object::DoDispose();
}

} // namespace ns3
