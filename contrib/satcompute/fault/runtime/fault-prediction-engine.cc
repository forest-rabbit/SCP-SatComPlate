/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-prediction-engine.h"

#include "ns3/fault-controller.h"
#include "ns3/fault-parameter-validator.h"

#include "../../common/time-conversion.h"
#include "../../task/compute-service.h"
#include "../../task/task-coordinator.h"
#include "../../topology/orbit/online-orbit-constellation.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <limits>
#include <set>
#include <string>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(FaultPredictionEngine);

TypeId
FaultPredictionEngine::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::FaultPredictionEngine")
                               .SetParent<Object>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<FaultPredictionEngine>();
    return typeId;
}

FaultPredictionEngine::FaultPredictionEngine() = default;

FaultPredictionEngine::~FaultPredictionEngine() = default;

void
FaultPredictionEngine::Configure(const FaultParameters& parameters,
                                 const std::vector<uint32_t>& computeNodeIds,
                                 int64_t simulationDurationNs,
                                 Ptr<FaultController> faultController)
{
    if (m_configured || !Simulator::Now().IsZero())
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine must be configured once at time zero");
    }
    ValidateFaultParameters(parameters);
    if ((!parameters.f1.enabled && !parameters.f2.enabled) ||
        simulationDurationNs <= 0 || faultController == nullptr)
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine requires F1 or F2, duration, and FaultController");
    }
    const std::set<uint32_t> uniqueNodeIds(computeNodeIds.begin(),
                                           computeNodeIds.end());
    if (uniqueNodeIds.empty() || uniqueNodeIds.size() != computeNodeIds.size())
    {
        throw FaultPredictionEngineError(
            "prediction compute node IDs must be non-empty and unique");
    }

    m_parameters = parameters;
    m_checkIntervalNs = SatComputeSecondsToNanoseconds(
        parameters.checkIntervalSeconds,
        "fault.checkIntervalSeconds");
    if (m_checkIntervalNs <= 0)
    {
        throw FaultPredictionEngineError(
            "prediction interval must convert to positive nanoseconds");
    }
    m_simulationDurationNs = simulationDurationNs;
    m_faultController = faultController;
    if (parameters.f1.enabled)
    {
        m_f1Model.emplace(parameters.f1);
    }
    if (parameters.f2.enabled)
    {
        m_f2Model.emplace(parameters.f2);
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
        m_nodes.emplace(nodeId, state);
    }

    for (int64_t timeNs = m_checkIntervalNs; timeNs < simulationDurationNs;)
    {
        m_predictionEvents.push_back(
            Simulator::Schedule(NanoSeconds(timeNs),
                                &FaultPredictionEngine::PrepareTime,
                                this,
                                timeNs));
        if (timeNs > std::numeric_limits<int64_t>::max() - m_checkIntervalNs)
        {
            break;
        }
        timeNs += m_checkIntervalNs;
    }
    m_configured = true;
}

void
FaultPredictionEngine::BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator)
{
    if (!m_configured || m_bound || taskCoordinator == nullptr)
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine task binding is invalid");
    }
    m_taskCoordinator = taskCoordinator;
    for (const Ptr<ComputeService>& service : taskCoordinator->GetComputeServices())
    {
        const auto node = m_nodes.find(service->GetNodeId());
        if (node != m_nodes.end())
        {
            if (node->second.computeService != nullptr)
            {
                throw FaultPredictionEngineError(
                    "FaultPredictionEngine compute node IDs must be unique");
            }
            node->second.computeService = service;
        }
    }
    for (const auto& [nodeId, state] : m_nodes)
    {
        if (state.computeService == nullptr)
        {
            throw FaultPredictionEngineError(
                "prediction model has no ComputeService for node_id=" +
                std::to_string(nodeId));
        }
    }
    m_bound = true;
}

void
FaultPredictionEngine::BindOrbitConstellation(
    const OnlineOrbitConstellation& constellation)
{
    if (!m_configured || !m_f2Model.has_value() || m_constellation != nullptr)
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine orbit binding is invalid");
    }
    for (auto& [nodeId, state] : m_nodes)
    {
        if (!constellation.GetIdMap().HasSatelliteId(nodeId))
        {
            throw FaultPredictionEngineError(
                "F2 prediction node is absent from the orbit constellation: " +
                std::to_string(nodeId));
        }
        m_f2Model->Update(state.f2State,
                          constellation.GetPosition(nodeId),
                          0.0);
    }
    m_constellation = &constellation;
}

void
FaultPredictionEngine::PrepareTime(int64_t simulationTimeNs)
{
    NS_ABORT_MSG_IF(!m_configured || !m_bound ||
                        (m_f2Model.has_value() && m_constellation == nullptr) ||
                        simulationTimeNs != Simulator::Now().GetNanoSeconds() ||
                        !m_preparedPredictions.empty(),
                    "FaultPredictionEngine prepare state is invalid");

    for (auto& [nodeId, state] : m_nodes)
    {
        if (!m_faultController->GetState().IsSatelliteAvailable(nodeId))
        {
            continue;
        }
        const bool controllerComputeAvailable =
            m_faultController->GetState().IsComputeAvailable(nodeId);
        if (m_f1Model.has_value())
        {
            const bool busy = controllerComputeAvailable &&
                              state.computeService->IsComputeAvailable() &&
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
        if (!controllerComputeAvailable ||
            !state.computeService->IsComputeAvailable())
        {
            continue;
        }
        const std::optional<RunningComputeTaskSnapshot> task =
            state.computeService->GetRunningTaskSnapshot();
        if (!task.has_value())
        {
            continue;
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
                    return m_constellation->GetPositionAt(
                        nodeId,
                        NanoSeconds(targetTimeNs));
                };
        }
        input.predictionTimeNs = simulationTimeNs;
        input.remainingComputeTimeNs = task->remainingTimeNs;
        input.checkIntervalNs = m_checkIntervalNs;
        PreparedPrediction prepared;
        prepared.taskId = task->taskId;
        prepared.taskStartTimeNs = task->startTimeNs;
        prepared.taskServiceTimeNs = task->serviceTimeNs;
        prepared.taskElapsedTimeNs = task->elapsedTimeNs;
        prepared.remainingTimeNs = task->remainingTimeNs;
        prepared.completionRatio = task->completionRatio;
        prepared.prediction = PredictComputeFailureBeforeFinish(input);
        m_preparedPredictions.emplace(nodeId, std::move(prepared));
    }

    // This receives a later event UID than all already scheduled work at the
    // timestamp, including fault/controller and task completion events.
    Simulator::ScheduleNow(&FaultPredictionEngine::FinalizeTime,
                           this,
                           simulationTimeNs);
}

void
FaultPredictionEngine::FinalizeTime(int64_t simulationTimeNs)
{
    NS_ABORT_MSG_IF(!m_configured || !m_bound ||
                        simulationTimeNs != Simulator::Now().GetNanoSeconds(),
                    "FaultPredictionEngine finalize state is invalid");
    std::map<uint32_t, ActiveRisk> decisionRisks = m_activeRisks;
    std::map<uint32_t, ActiveRisk> sameTickStartedRisks;
    const std::vector<FaultRuntimeEventRecord>& events =
        m_faultController->GetEvents();
    NS_ABORT_MSG_IF(m_consumedFaultEventCount > events.size(),
                    "FaultPredictionEngine consumed an invalid event prefix");
    for (; m_consumedFaultEventCount < events.size();
         ++m_consumedFaultEventCount)
    {
        const FaultRuntimeEventRecord& event = events[m_consumedFaultEventCount];
        NS_ABORT_MSG_IF(event.simulationTimeNs > simulationTimeNs,
                        "FaultPredictionEngine observed a future fault event");
        if (event.eventType == FaultEventType::NOTICE)
        {
            NS_ABORT_MSG_IF(event.faultType != FaultType::COMPUTE ||
                                !event.noticeTimeNs.has_value() ||
                                event.noticeTimeNs.value() != event.simulationTimeNs ||
                                m_activeRisks.contains(event.nodeId),
                            "FaultPredictionEngine received an invalid NOTICE");
            const ActiveRisk risk{event.faultId, event.noticeTimeNs.value()};
            m_activeRisks.emplace(event.nodeId, risk);
            decisionRisks[event.nodeId] = risk;
            continue;
        }
        if (event.eventType == FaultEventType::NOTICE_CLEAR)
        {
            const auto active = m_activeRisks.find(event.nodeId);
            if (active != m_activeRisks.end() &&
                active->second.faultId == event.faultId)
            {
                m_activeRisks.erase(active);
                decisionRisks.erase(event.nodeId);
            }
            continue;
        }
        if (event.eventType != FaultEventType::START)
        {
            continue;
        }
        const auto active = m_activeRisks.find(event.nodeId);
        if (active == m_activeRisks.end())
        {
            continue;
        }
        const bool matchingComputeRisk =
            event.faultType == FaultType::COMPUTE &&
            active->second.faultId == event.faultId;
        if (matchingComputeRisk && event.simulationTimeNs == simulationTimeNs)
        {
            sameTickStartedRisks[event.nodeId] = active->second;
        }
        if (matchingComputeRisk || event.faultType == FaultType::SATELLITE)
        {
            m_activeRisks.erase(active);
            decisionRisks.erase(event.nodeId);
        }
    }

    for (const auto& [nodeId, prepared] : m_preparedPredictions)
    {
        const auto started = sameTickStartedRisks.find(nodeId);
        const auto active = decisionRisks.find(nodeId);
        if (started == sameTickStartedRisks.end() && active == decisionRisks.end())
        {
            continue;
        }
        const ActiveRisk& risk = started != sameTickStartedRisks.end()
                                     ? started->second
                                     : active->second;
        NS_ABORT_MSG_IF(simulationTimeNs < risk.noticeTimeNs ||
                            prepared.remainingTimeNs >
                                std::numeric_limits<int64_t>::max() - simulationTimeNs,
                        "FaultPredictionEngine record timing is invalid");
        const ComputeFailurePrediction& prediction = prepared.prediction;
        m_predictionRecords.push_back(
            {simulationTimeNs,
             risk.faultId,
             nodeId,
             prepared.taskId,
             risk.noticeTimeNs,
             simulationTimeNs - risk.noticeTimeNs,
             prepared.taskStartTimeNs,
             prepared.taskServiceTimeNs,
             prepared.taskElapsedTimeNs,
             prepared.remainingTimeNs,
             simulationTimeNs + prepared.remainingTimeNs,
             prepared.completionRatio,
             prediction.f1StepFailureProbability,
             prediction.f2StepFailureProbability,
             prediction.combinedStepFailureProbability,
             prediction.horizonStepCount,
             prediction.predictedFailureProbability});
    }
    m_preparedPredictions.clear();
}

const std::vector<ComputeFailurePredictionRecord>&
FaultPredictionEngine::GetPredictionRecords() const
{
    if (!m_configured)
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine is not configured");
    }
    return m_predictionRecords;
}

void
FaultPredictionEngine::DoDispose()
{
    for (EventId& event : m_predictionEvents)
    {
        if (event.IsPending())
        {
            Simulator::Cancel(event);
        }
    }
    m_nodes.clear();
    m_activeRisks.clear();
    m_preparedPredictions.clear();
    m_faultController = nullptr;
    m_taskCoordinator = nullptr;
    m_constellation = nullptr;
    Object::DoDispose();
}

} // namespace ns3
