/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_MODEL_ENGINE_H
#define SATCOMPUTE_FAULT_MODEL_ENGINE_H

#include "fault-controller.h"

#include "ns3/compute-failure-probability-record.h"
#include "ns3/compute-failure-predictor.h"
#include "ns3/fault-para.h"
#include "ns3/compute-fault-combination.h"
#include "ns3/f1-self-state-fault-model.h"
#include "ns3/f2-radiation-fault-model.h"
#include "ns3/f3-debris-fault-model.h"

#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ComputeService;
class OnlineOrbitConstellation;
class TaskCoordinator;

/** Causal pre-sample task state. No outcome, schedule, or future workload is exposed. */
struct FaultEpochInput
{
    uint32_t nodeId{}; ///< Stable compute node.
    uint64_t taskId{}; ///< Currently executing task.
    double currentSampleProbability{}; ///< Exact probability used by the current sampler.
    ComputeFailurePredictionInput prediction; ///< Canonical predictor input from live state.
};

/** Observed result delivered only after the complete same-time fault batch is applied. */
struct FaultEpochOutcome
{
    uint32_t nodeId{}; ///< Stable compute node.
    uint64_t taskId{}; ///< Pre-sample executing task.
    bool sampled{}, faultHit{}; ///< F1/F2 sampling eligibility and actual current node hit.
};

/** Risk observed immediately before an actual F3; never an extra sampling event. */
struct F3ComputeRiskRecord
{
    int64_t timeNs{};
    uint32_t nodeId{};
    uint64_t taskId{};
    double pF1{}, pF2{}, qCompute{}, pFinish{};
};

/** Availability of a read-only node-level forecast. */
enum class ComputeRiskStatus
{
    AVAILABLE,
    UNAVAILABLE,
    NOT_READY
};

/** Conditional F1/F2 risk over (asOfTimeNs, asOfTimeNs + horizonNs].
 * The current busy/idle condition is held constant; no future workload is read.
 * F3 is excluded from the probability and reported only after actual failure.
 */
struct ComputeRiskSnapshot
{
    uint32_t nodeId{};    ///< Stable external satellite ID.
    int64_t asOfTimeNs{}; ///< Current simulation timestamp.
    int64_t horizonNs{};  ///< Requested future interval, not task remaining time.
    ComputeRiskStatus status{ComputeRiskStatus::NOT_READY}; ///< Query readiness.
    bool permanentlyUnavailable{};                          ///< Actual permanent satellite failure.
    uint64_t checkCount{};          ///< Scheduled model checks in the interval.
    std::optional<double> pF1;      ///< Conditional F1 interval probability.
    std::optional<double> pF2;      ///< Conditional F2 interval probability.
    std::optional<double> pCompute; ///< Independent F1/F2 union, same horizon.
    bool operator==(const ComputeRiskSnapshot&) const = default;
};

/** Configuration or lifecycle error raised by the online fault-model engine. */
class FaultModelEngineError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** Observable end-of-run fault-model state for one configured compute node. */
struct FaultModelNodeSnapshot
{
    uint32_t nodeId{}; ///< Stable external satellite ID.
    F1SelfStateFaultSnapshot f1State; ///< Current pure F1 state.
    F2RadiationFaultSnapshot f2State; ///< Current pure F2 state.
    double combinedStepFailureProbability{}; ///< Current q_comp output.
    uint64_t f1SampleCount{}; ///< Number of independent F1 random draws.
    uint64_t f2SampleCount{}; ///< Number of independent F2 random draws.
    uint64_t f1OccurrenceCount{}; ///< Number of F1 source hits.
    uint64_t f2OccurrenceCount{}; ///< Number of F2 source hits.
    uint64_t computeFaultCount{}; ///< Number of coalesced compute faults.
    bool computeAvailable{true}; ///< Final N4A compute availability.
};

/** Evaluate fault models online and submit events to the N4A controller. */
struct FaultModelStateRecord
{
    int64_t timeNs{}; ///< Actual model check time.
    uint32_t nodeId{}; ///< Stable satellite ID.
    F1SelfStateFaultSnapshot f1; ///< State after this check's thermal update.
    F2RadiationFaultSnapshot f2; ///< Native position-driven state at this check.
    bool samplingEligible{}; ///< False during compute outage or a same-time F3.
};

/** Evaluate fault models online and submit events to the controller. */
class FaultModelEngine : public Object
{
  public:
    /** @return ns-3 runtime type information. */
    static TypeId GetTypeId();

    FaultModelEngine();
    ~FaultModelEngine() override;

    /**
     * Configure model state and deterministic per-node random streams at time zero.
     *
     * @param parameters Strictly validated built-in fault parameters.
     * @param satelliteIds Complete stable-ID constellation universe.
     * @param computeNodeIds Stable IDs present in the compute profile.
     * @param simulationDurationNs Exclusive simulation end in nanoseconds.
     * @param faultController N4A controller configured for online generation.
     * @param probabilityAuditEnabled Whether to collect live probability audit records.
     */
    void Configure(const FaultParameters& parameters,
                   const std::vector<uint32_t>& satelliteIds,
                   const std::vector<uint32_t>& computeNodeIds,
                   int64_t simulationDurationNs,
                   Ptr<FaultController> faultController,
                   bool probabilityAuditEnabled);

    /** Bind live compute services after TaskCoordinator initialization. */
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    /** Bind and initialize the shared native orbit source required by F2. */
    void BindOrbitConstellation(const OnlineOrbitConstellation& constellation);

    /** Optional synchronous observer pair; absent in off/fixed modes. Never owns RNG. */
    void SetEpochObservers(std::function<void(const FaultEpochInput&)> before,
                           std::function<void(int64_t, const std::vector<FaultEpochOutcome>&)> after);

    /** Return the canonical occurred-event trace. */
    const FaultTrace& Finalize();

    /** Return node snapshots in ascending stable-node-ID order. */
    std::vector<FaultModelNodeSnapshot> GetNodeSnapshots() const;

    /**
     * Query a node without RNG, state mutation, NOTICE gating or audit dependency.
     * Pure projection uses the same discrete check grid as generation, excluding
     * a check at as-of time and including a check at the horizon endpoint.
     * Call after the model event at a shared timestamp to observe that event.
     * Invalid/overflowing horizons throw; unknown/unbound/finalized nodes are not ready.
     * @param nodeId Stable compute node ID.
     * @param horizonNs Positive future interval (default one second).
     * @return Risk conditional on the current load and no intervening F3 event.
     */
    ComputeRiskSnapshot QueryComputeRisk(uint32_t nodeId, int64_t horizonNs = 1000000000LL) const;

    /** Current causal copies and actual next sampling point; no RNG or future F3 access. */
    std::optional<ComputeFailurePredictionInput> QueryTaskPrediction(uint32_t nodeId,
                                                                     int64_t remainingNs) const;

    const std::vector<F3ComputeRiskRecord>& GetF3ComputeRiskRecords() const
    {
        return m_f3RiskRecords;
    }

    /** @return Pre-sampling probabilities produced from live generate state. */
    const std::vector<ComputeFailureProbabilityRecord>& GetProbabilityRecords() const;
    /** @return Optional state samples collected only with probability audit enabled. */
    const std::vector<FaultModelStateRecord>& GetStateAuditRecords() const;

  private:
    /** Online model, random stream, and compute-service binding for one node. */
    struct NodeState
    {
        F1SelfStateFaultSnapshot f1State; ///< Current pure F1 state.
        F2RadiationFaultSnapshot f2State; ///< Current pure F2 state.
        int64_t modelTimeNs{};            ///< Last completed physical model update.
        int64_t thermalTimeNs{}; ///< Exact last busy/idle physical update.
        Ptr<UniformRandomVariable> f1Random; ///< Stable per-node F1 sampling stream.
        Ptr<UniformRandomVariable> f2Random; ///< Stable per-node F2 sampling stream.
        uint64_t f1SampleCount{}; ///< Independent F1 draws consumed.
        uint64_t f2SampleCount{}; ///< Independent F2 draws consumed.
        uint64_t f1OccurrenceCount{}; ///< F1 source hits before coalescing.
        uint64_t f2OccurrenceCount{}; ///< F2 source hits before coalescing.
        uint64_t computeFaultCount{}; ///< Coalesced compute-fault starts.
        std::optional<FaultDefinition> activeComputeFault; ///< Recoverable fault in flight.
        Ptr<ComputeService> computeService; ///< Live busy/idle source.
    };

    /** Build one occurred recoverable compute-fault record. */
    FaultDefinition MakeComputeFault(uint32_t nodeId,
                                     uint64_t faultId,
                                     double currentProbability,
                                     int64_t startTimeNs) const;
    /** Build one permanent, unannounced F3 satellite fault. */
    FaultDefinition MakeSatelliteFault(uint32_t nodeId,
                                       uint64_t faultId,
                                       int64_t startTimeNs) const;
    /** End an active compute outage at a superseding F3 timestamp. */
    void ShortenActiveComputeFault(NodeState& state,
                                   int64_t simulationTimeNs);
    /** Record one running-task forecast from live state before random draws. */
    void RecordProbability(uint32_t nodeId,
                           const NodeState& state,
                           int64_t simulationTimeNs);
    /** Shared immutable input for audit and the causal pre-sample observer. */
    ComputeFailurePredictionInput PredictionInput(uint32_t nodeId,
                                                   const NodeState& state,
                                                   int64_t timeNs,
                                                   int64_t remainingNs) const;
    /** Update periodic models and/or execute F3 events in one timestamp batch. */
    void ProcessTime(int64_t simulationTimeNs, bool updateComputeModels);
    /** Observe exact service transitions without RNG or business mutations. */
    void OnComputeStateChanged(uint32_t nodeId, bool busy);
    void DoDispose() override;

    bool m_configured{}; ///< Whether Configure completed.
    bool m_bound{}; ///< Whether compute services were bound.
    bool m_finalized{}; ///< Whether the trace was closed.
    bool m_probabilityAuditEnabled{}; ///< Whether live audit records are collected.
    int64_t m_simulationDurationNs{}; ///< Exclusive simulation end.
    FaultParameters m_parameters; ///< Unified model parameters.
    int64_t m_checkIntervalNs{}; ///< Converted model-check interval.
    int64_t m_lastCheckStartedNs{-1}; ///< Resolves coincident task start / actual check phases.
    int64_t m_recoveryDurationNs{}; ///< Converted compute outage duration.
    std::optional<F1SelfStateFaultModel> m_f1Model; ///< Active F1 pure model.
    std::optional<F2RadiationFaultModel> m_f2Model; ///< Active F2 pure model.
    std::optional<F3DebrisFaultModel> m_f3Model; ///< Active F3 schedule model.
    std::map<uint32_t, NodeState> m_nodes; ///< Node state in stable-ID order.
    std::map<int64_t, std::vector<uint32_t>> m_f3EventsByTime; ///< F3 schedule.
    uint64_t m_nextFaultId{1}; ///< Next trace identity.
    FaultTrace m_trace; ///< Completed canonical trace records.
    std::vector<ComputeFailureProbabilityRecord> m_probabilityRecords; ///< Live probabilities.
    std::vector<FaultModelStateRecord> m_stateAuditRecords; ///< Optional observed state, no RNG.
    std::vector<F3ComputeRiskRecord> m_f3RiskRecords;       ///< Actual F3 causal diagnostic only.
    std::vector<EventId> m_modelEvents; ///< Pre-scheduled model/F3 checks.
    Ptr<FaultController> m_faultController; ///< Sole runtime fault executor.
    Ptr<TaskCoordinator> m_taskCoordinator; ///< Bound task lifecycle owner.
    const OnlineOrbitConstellation* m_constellation{}; ///< Shared native F2 positions.
    std::function<void(const FaultEpochInput&)> m_beforeEpoch; ///< Pre-draw, read-only proposal.
    std::function<void(int64_t, const std::vector<FaultEpochOutcome>&)> m_afterEpoch;
    ///< Post-application commit boundary; no reliance on event UID.
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_MODEL_ENGINE_H
