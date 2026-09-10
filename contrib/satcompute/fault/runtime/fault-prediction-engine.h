/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_PREDICTION_ENGINE_H
#define SATCOMPUTE_FAULT_PREDICTION_ENGINE_H

#include "ns3/compute-failure-probability-record.h"
#include "ns3/compute-failure-predictor.h"
#include "ns3/event-id.h"
#include "ns3/fault-para.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ComputeService;
class FaultController;
class OnlineOrbitConstellation;
class TaskCoordinator;

/** Configuration or lifecycle error raised by the online prediction engine. */
class FaultPredictionEngineError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Maintain private F1/F2 state and audit every eligible running-task check.
 * No random stream is consumed, no live model is mutated, and no notification
 * or future fault schedule is read. The current pre-sampling check is included.
 */
class FaultPredictionEngine : public Object
{
  public:
    /** @return ns-3 runtime type information. */
    static TypeId GetTypeId();

    FaultPredictionEngine();
    ~FaultPredictionEngine() override;

    /**
     * Configure shadow models and pre-schedule prediction checks at time zero.
     *
     * Call this before FaultController and FaultModelEngine configuration so
     * PrepareTime executes before their events at the same timestamp.
     *
     * @param parameters Same built-in F1/F2 parameters used by fault generation.
     * @param computeNodeIds Stable compute-node IDs whose state is shadowed.
     * @param simulationDurationNs Exclusive simulation end.
     * @param faultController Runtime source of already executed fault events.
     */
    void Configure(const FaultParameters& parameters,
                   const std::vector<uint32_t>& computeNodeIds,
                   int64_t simulationDurationNs,
                   Ptr<FaultController> faultController);

    /** Bind live compute services after TaskCoordinator initialization. */
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    /** Bind and initialize native future positions required by F2. */
    void BindOrbitConstellation(const OnlineOrbitConstellation& constellation);

    /** @return Formal prediction records accumulated so far. */
    const std::vector<ComputeFailureProbabilityRecord>& GetPredictionRecords() const;

  private:
    /** Independent, deterministic model state for one compute node. */
    struct NodeState
    {
        F1SelfStateFaultSnapshot f1State; ///< Current F1 shadow state.
        int64_t thermalTimeNs{}; ///< Exact physical update boundary.
        F2RadiationFaultSnapshot f2State; ///< Current F2 shadow state.
        Ptr<ComputeService> computeService; ///< Live task-state source.
    };

    /** Forecast prepared before same-time fault and task events execute. */
    struct PreparedPrediction
    {
        uint64_t taskId{};
        int64_t taskStartTimeNs{};
        int64_t taskServiceTimeNs{};
        int64_t taskElapsedTimeNs{};
        int64_t remainingTimeNs{};
        double completionRatio{};
        ComputeFailurePrediction prediction;
    };

    /** Advance shadow state and prepare forecasts before same-time model events. */
    void PrepareTime(int64_t simulationTimeNs);
    /** Track actual busy/idle boundaries in the private shadow state. */
    void OnComputeStateChanged(uint32_t nodeId, bool busy);
    /** Emit prepared pre-sampling records without notification gating. */
    void FinalizeTime(int64_t simulationTimeNs);
    void DoDispose() override;

    bool m_configured{}; ///< Whether Configure completed.
    bool m_bound{}; ///< Whether live compute services were bound.
    int64_t m_checkIntervalNs{}; ///< Shared F1/F2 cadence.
    int64_t m_simulationDurationNs{}; ///< Exclusive simulation end.
    FaultParameters m_parameters; ///< Shared immutable prediction parameters.
    std::optional<F1SelfStateFaultModel> m_f1Model; ///< Active F1 kernel.
    std::optional<F2RadiationFaultModel> m_f2Model; ///< Active F2 kernel.
    std::map<uint32_t, NodeState> m_nodes; ///< Shadow state by stable node ID.
    std::map<uint32_t, PreparedPrediction> m_preparedPredictions; ///< Current tick.
    std::vector<ComputeFailureProbabilityRecord> m_predictionRecords; ///< Past output.
    std::vector<EventId> m_predictionEvents; ///< Pre-scheduled prepare checks.
    Ptr<FaultController> m_faultController; ///< Time-gated event source.
    Ptr<TaskCoordinator> m_taskCoordinator; ///< Compute-service lifecycle owner.
    const OnlineOrbitConstellation* m_constellation{}; ///< Native F2 positions.
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_PREDICTION_ENGINE_H
