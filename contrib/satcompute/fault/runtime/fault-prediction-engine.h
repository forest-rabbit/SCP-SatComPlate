/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_PREDICTION_ENGINE_H
#define SATCOMPUTE_FAULT_PREDICTION_ENGINE_H

#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ComputeService;
class FaultController;
class TaskCoordinator;

/** Configuration or lifecycle error raised by the online prediction engine. */
class FaultPredictionEngineError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** One causal F1/F2 probability forecast for a currently running task. */
struct ComputeFailurePredictionRecord
{
    int64_t simulationTimeNs{}; ///< Time at which this forecast became visible.
    uint64_t faultId{}; ///< Active risk-episode identity.
    uint32_t nodeId{}; ///< Stable compute-satellite ID.
    uint64_t taskId{}; ///< Stable running-task ID.
    int64_t noticeTimeNs{}; ///< Already observed risk-entry time.
    int64_t riskElapsedTimeNs{}; ///< simulation_time - notice_time.
    int64_t taskComputeStartTimeNs{}; ///< Already observed compute-dispatch time.
    int64_t taskServiceTimeNs{}; ///< Fixed task compute duration.
    int64_t taskElapsedTimeNs{}; ///< Known compute progress in nanoseconds.
    int64_t remainingComputeTimeNs{}; ///< Known time to scheduled completion.
    int64_t expectedComputeCompletionTimeNs{}; ///< Known scheduled completion time.
    double completionRatio{}; ///< Known task completion ratio in [0, 1].
    double combinedStepFailureProbability{}; ///< q_comp exposed by NOTICE.
    uint64_t horizonStepCount{}; ///< Fault checks through compute completion.
    double predictedFailureProbability{}; ///< P(compute failure before completion).
};

/**
 * Convert time-gated NOTICE state and live task progress into probability forecasts.
 *
 * The engine reads only FaultController events that have already executed. It
 * never receives fault_occurred, a future START time, warning lead time, or the
 * realized risk duration before those facts become observable.
 */
class FaultPredictionEngine : public Object
{
  public:
    /** @return ns-3 runtime type information. */
    static TypeId GetTypeId();

    FaultPredictionEngine();
    ~FaultPredictionEngine() override;

    /**
     * Schedule causal prediction checks from time zero.
     *
     * Call this before scheduling FaultController/model events so a forecast
     * from an already active risk episode is recorded before a same-time START.
     *
     * @param checkIntervalNs Positive prediction/fault check interval.
     * @param simulationDurationNs Exclusive simulation end.
     * @param faultController Runtime source of already executed fault events.
     */
    void Configure(int64_t checkIntervalNs,
                   int64_t simulationDurationNs,
                   Ptr<FaultController> faultController);

    /** Bind live compute services after TaskCoordinator initialization. */
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    /** @return Forecast records accumulated up to the current simulation time. */
    const std::vector<ComputeFailurePredictionRecord>& GetPredictionRecords() const;

  private:
    /** Minimal causal state retained from one executed NOTICE. */
    struct ActiveRisk
    {
        uint64_t faultId{}; ///< Risk-episode identity.
        int64_t noticeTimeNs{}; ///< Observed risk-entry time.
        double combinedStepFailureProbability{}; ///< q_comp visible at NOTICE.
    };

    /** Consume only runtime fault events visible by the current time. */
    void RefreshActiveRisks(int64_t simulationTimeNs);
    /** Record forecasts for active risks and currently running tasks. */
    void ProcessTime(int64_t simulationTimeNs);
    void DoDispose() override;

    bool m_configured{}; ///< Whether Configure completed.
    bool m_bound{}; ///< Whether live compute services were bound.
    int64_t m_checkIntervalNs{}; ///< Shared prediction/fault cadence.
    int64_t m_simulationDurationNs{}; ///< Exclusive simulation end.
    std::size_t m_consumedFaultEventCount{}; ///< Visible controller-event prefix.
    std::map<uint32_t, ActiveRisk> m_activeRisks; ///< Active risk by stable node ID.
    std::map<uint32_t, Ptr<ComputeService>> m_computeServices; ///< Live service by node.
    std::vector<ComputeFailurePredictionRecord> m_predictionRecords; ///< Past forecasts.
    std::vector<EventId> m_predictionEvents; ///< Pre-scheduled causal checks.
    Ptr<FaultController> m_faultController; ///< Time-gated risk event source.
    Ptr<TaskCoordinator> m_taskCoordinator; ///< Compute-service lifecycle owner.
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_PREDICTION_ENGINE_H
