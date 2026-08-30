/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-prediction-engine.h"

#include "ns3/compute-failure-predictor.h"
#include "ns3/fault-controller.h"

#include "../../task/compute-service.h"
#include "../../task/task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <cmath>
#include <limits>
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
FaultPredictionEngine::Configure(int64_t checkIntervalNs,
                                 int64_t simulationDurationNs,
                                 Ptr<FaultController> faultController)
{
    if (m_configured || !Simulator::Now().IsZero())
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine must be configured once at time zero");
    }
    if (checkIntervalNs <= 0 || simulationDurationNs <= 0 ||
        faultController == nullptr)
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine requires positive intervals and FaultController");
    }
    m_checkIntervalNs = checkIntervalNs;
    m_simulationDurationNs = simulationDurationNs;
    m_faultController = faultController;
    for (int64_t timeNs = checkIntervalNs; timeNs < simulationDurationNs;)
    {
        m_predictionEvents.push_back(
            Simulator::Schedule(NanoSeconds(timeNs),
                                &FaultPredictionEngine::ProcessTime,
                                this,
                                timeNs));
        if (timeNs > std::numeric_limits<int64_t>::max() - checkIntervalNs)
        {
            break;
        }
        timeNs += checkIntervalNs;
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
        if (!m_computeServices.emplace(service->GetNodeId(), service).second)
        {
            throw FaultPredictionEngineError(
                "FaultPredictionEngine compute node IDs must be unique");
        }
    }
    if (m_computeServices.empty())
    {
        throw FaultPredictionEngineError(
            "FaultPredictionEngine requires at least one compute service");
    }
    m_bound = true;
}

void
FaultPredictionEngine::RefreshActiveRisks(int64_t simulationTimeNs)
{
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
                                !event.failureProbability.has_value() ||
                                !std::isfinite(event.failureProbability.value()) ||
                                event.failureProbability.value() < 0.0 ||
                                event.failureProbability.value() > 1.0 ||
                                m_activeRisks.contains(event.nodeId),
                            "FaultPredictionEngine received an invalid NOTICE");
            m_activeRisks.emplace(
                event.nodeId,
                ActiveRisk{event.faultId,
                           event.noticeTimeNs.value(),
                           event.failureProbability.value()});
            continue;
        }
        if (event.eventType == FaultEventType::NOTICE_CLEAR ||
            event.eventType == FaultEventType::START)
        {
            const auto active = m_activeRisks.find(event.nodeId);
            if (active != m_activeRisks.end() &&
                (event.faultType == FaultType::SATELLITE ||
                 active->second.faultId == event.faultId))
            {
                m_activeRisks.erase(active);
            }
        }
    }
}

void
FaultPredictionEngine::ProcessTime(int64_t simulationTimeNs)
{
    NS_ABORT_MSG_IF(!m_configured || !m_bound ||
                        simulationTimeNs != Simulator::Now().GetNanoSeconds(),
                    "FaultPredictionEngine scheduled state is invalid");
    RefreshActiveRisks(simulationTimeNs);
    for (const auto& [nodeId, risk] : m_activeRisks)
    {
        NS_ABORT_MSG_IF(simulationTimeNs < risk.noticeTimeNs,
                        "FaultPredictionEngine risk starts in the future");
        if (!m_faultController->GetState().IsSatelliteAvailable(nodeId) ||
            !m_faultController->GetState().IsComputeAvailable(nodeId))
        {
            continue;
        }
        const auto service = m_computeServices.find(nodeId);
        NS_ABORT_MSG_IF(service == m_computeServices.end(),
                        "FaultPredictionEngine risk node has no compute service");
        const std::optional<RunningComputeTaskSnapshot> task =
            service->second->GetRunningTaskSnapshot();
        if (!task.has_value())
        {
            continue;
        }
        const ComputeFailurePrediction prediction =
            PredictComputeFailureBeforeFinish(
                risk.combinedStepFailureProbability,
                task->remainingTimeNs,
                m_checkIntervalNs);
        NS_ABORT_MSG_IF(task->remainingTimeNs >
                            std::numeric_limits<int64_t>::max() - simulationTimeNs,
                        "FaultPredictionEngine completion time overflow");
        m_predictionRecords.push_back(
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
             prediction.combinedStepFailureProbability,
             prediction.horizonStepCount,
             prediction.predictedFailureProbability});
    }
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
    m_computeServices.clear();
    m_faultController = nullptr;
    m_taskCoordinator = nullptr;
    Object::DoDispose();
}

} // namespace ns3
