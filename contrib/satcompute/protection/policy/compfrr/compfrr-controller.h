/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_CONTROLLER_H
#define SATCOMPUTE_COMPFRR_CONTROLLER_H
#include "../../../fault/runtime/fault-model-engine.h"
#include "../placement/fa-first-feasible/fa-first-feasible-placement-policy.h"
#include "frequency/frequency-decision-gate.h"
#include "storage-estimator.h"
#include "../../runtime/recovery-controller.h"
#include "../../runtime/placement-load-ledger.h"
#include "../../runtime/decision-path-snapshot.h"
#include "placement/compfrr-placement-tracker.h"
#include "input/input-start-audit.h"
#include "input/input-admission-policy.h"
#include "../../mechanism/input-staging/input-staging-manager.h"

#include <filesystem>
#include <set>

namespace ns3::protection
{
/** Proposal and observed resolution; never included in actual cost accounting. */
struct FrequencyDecisionRecord
{
    uint64_t taskId{}, progressWork{}; ///< Logical task and actual WU.
    TaskProfile profile{};             ///< Frozen task class.
    FrequencyInput input;       ///< Causal scalar snapshot; estimator released after evaluation.
    FrequencyDecision proposal; ///< Exact solver result before sampling.
    std::optional<PlacementDecision> pair; ///< Proposed OFF or fixed ON pair.
    std::optional<FrequencyConfiguration> previous, committedConfig; ///< Effective cadence history.
    bool sampled{}, faultHit{}, committed{}; ///< Observed current outcome and gate result.
    ProtectionPhase phaseAfter{};            ///< Effective phase after current fault application.
    std::string reason;                      ///< Resolution, distinct from solver reason.
    std::string resourceReason; ///< Diagnostic hard-resource cause; never changes solver scoring.
    std::string replayReason;   ///< Soft OFF INPUT admission status.
    bool resourceHold{}; ///< ON config rejection, not blanket checkpoint maintenance PAUSE.
    PlacementNodeLoad localLoad, remoteLoad; ///< Causal load snapshots before ranking/admission.
    std::string trigger{"FAULT_EPOCH"};      ///< TASK_RUNNING is a policy event, never a draw.
    double pF1{}, pF2{};     ///< Current model snapshot, not an observed failure label.
    int64_t firstSampleNs{}; ///< First real check included by the predictor.
    FeasiblePlacementPairs pairStats; ///< Exhaustive node/path feasibility, without storing pairs.
    uint64_t pairPathFeasible{}, pairHardChecked{}, pairHardFeasible{}, pairSkipStorage{}, pairSkipDeadline{};
    bool waitingBefore{}, waitingAfter{};
    uint64_t capacityRetryCount{};
    bool capacityRetrySuccess{};
    int64_t capacityWaitStartNs{-1}, capacityWaitEndNs{-1};
    std::optional<size_t> n5cTrace; ///< START-only spatial proposal; no solver call per remote.
    std::optional<uint64_t> n5cPeak; ///< Total replacement quota, captured before physical admission.
    std::optional<InputStartValidatedResources> inputAuditValidation; ///< Opt-in copy only, never decision input.
};

/** Online generate integration. Owns no fault model, RNG, state bytes or second network. */
class CompFrrController : public ProtectionPolicy
{
  public:
    /** Connect real epoch boundaries, existing checkpoint engine and single-attempt recovery. */
    CompFrrController(Ptr<TaskCoordinator> tasks,
                                  SatelliteRuntimeView& topology,
                                  Ptr<FaultModelEngine> faults,
                                  uint64_t capacity,
                                  int64_t stopNs,
                                  std::unique_ptr<PlacementPolicy> placement = nullptr,
                                  RemoteBusyRecoveryPolicy busyPolicy = RemoteBusyRecoveryPolicy::RELOCATE,
                                  InputStagingPolicy inputPolicy = InputStagingPolicy::EAGER,
                                  bool observePlacementResources = false,
                                  bool inputStartAudit = false,
                                  InputAdmissionPolicy inputAdmission = InputAdmissionPolicy::NONE);
    ~CompFrrController() override;
    /** Finish the same actual ledgers as fixed protection. */
    void Finalize();
    /** Write only decision/prediction audit, not actual metrics. */
    void WriteDecisions(const std::filesystem::path& directory) const;
    /** Opt-in development output only; no existing metrics/schema are changed. */
    void WriteInputStartAudit(const std::filesystem::path& directory) const;
    void WriteInputAdmissionAudit(const std::filesystem::path& directory) const;
    const InputStagingManager* OptionalInput() const { return m_optionalInput.get(); }
    const std::vector<InputStartAuditRecord>& InputStartRecords() const { return m_inputStartRecords; }
    const PlacementLoadLedger& PlacementLoads() const { return m_loads; }
    const PlacementPolicy& Placement() const { return *m_placement; }
    const CompFrrPlacementTracker* N5c() const { return m_n5c; }
    ///< Live ownership used by LRL and the same diagnostic output for FFP.

    const CheckpointManager& Manager() const
    {
        return m_manager;
    } ///< Actual checkpoint owner.

    const RecoveryController* Recovery() const
    {
        return m_recovery.get();
    } ///< Existing recovery.

    const std::vector<FrequencyDecisionRecord>& Decisions() const
    {
        return m_decisions;
    }

    ///< Stable causal decision order.
    ProtectionAction OnTaskComputeStart(const ProtectionContext&) override
    {
        return {};
    }

    ProtectionAction OnProtectionEpoch(const ProtectionContext&) override
    {
        return {};
    }

    ProtectionAction OnComputeFault(const ProtectionContext& context) override;

    void OnTaskComputeComplete(AttemptKey) override
    {
    }

    void OnTaskTerminal(uint64_t) override
    {
    }

  private:
    friend struct FrequencyRuntimeTestAccess; ///< Test-only controlled epoch boundary injection.

    struct State
    {
        FrequencyDecisionGate gate;            ///< Proposal does not mutate actual mechanism state.
        std::optional<PlacementDecision> pair; ///< Fixed only after START survives.
        std::optional<size_t> pending; ///< Decision retained through synchronous fault observers.
        std::optional<ProtectionPhase> stopped; ///< Deferred gate stop until proposal resolves.
        std::optional<int64_t> pauseStart; ///< Beginning of the current reason-specific interval.
        std::string pauseReason; ///< Current effective pause cause.
        bool pauseResourceHold{}; ///< Configuration rejection, with maintenance independently gated.
        int64_t lastDecisionNs{-1}; ///< Avoid duplicate policy evaluation at a coincident check.
        int64_t lastCapacityDecisionNs{-1}; ///< At most one capacity retry per task/timestamp.
        std::optional<int64_t> capacityWaitStart;
        uint64_t capacityRetryCount{};
    };
    struct PauseInterval
    {
        uint64_t taskId{}; int64_t startNs{}, endNs{}; std::string reason;
        bool resourceHold{};
    }; ///< Nonoverlapping intervals ending on resume, reason change, stop or finalization.

    struct Path
    {
        double bytesPerSecond{}, propagationSeconds{}; ///< Current bottleneck and path delay.
        bool local{}; ///< Source equals destination: no real network transfer.
        double Seconds(uint64_t bytes) const; ///< Serialization plus current propagation.
    };

    void OnTask(const TaskEventRecord& event);      ///< Immediate OFF decision at primary dispatch.
    void CapacityReleased();
    void DrainCapacityRetries();
    void CloseCapacityWait(uint64_t taskId, State& state, int64_t timeNs, const std::string& reason);
    void BeforeEpoch(const FaultEpochInput& epoch); ///< Pure proposal before random draws.
    void Evaluate(const FaultEpochInput& epoch, const std::string& trigger);
    void AfterEpoch(int64_t timeNs, const std::vector<FaultEpochOutcome>& outcomes);
    ///< Sole post-fault decision application boundary.
    void Initialized(uint64_t taskId);                ///< Real physical initialization callback.
    void ClosePause(uint64_t taskId, State& state, int64_t timeNs); ///< Close only observed time.
    const TaskRuntime& Task(uint64_t taskId) const;   ///< Stable logical task lookup.
    Ptr<ComputeService> Service(uint32_t node) const; ///< Actual compute profile owner.
    std::vector<BackupCandidate> Candidates(uint32_t primary) const; ///< Causal FFP inputs.
    std::optional<Path> EstimatePath(DecisionPathSnapshot& paths, uint32_t source,
                                     uint32_t destination,
                                     std::string* reason = nullptr) const;
    ///< Current deterministic route, never future queue completion.
    bool BuildResources(FrequencyDecisionRecord& row, const TaskRuntime& task, State& state,
                        DecisionPathSnapshot& paths);
    void EvaluateOffPairs(FrequencyDecisionRecord& row, const TaskRuntime& task, State& state,
                          DecisionPathSnapshot& paths);
    void SelectN5cRemote(FrequencyDecisionRecord& row, const TaskRuntime& task, State& state,
                         DecisionPathSnapshot& paths, const std::vector<PlacementDecision>& pairs);
    std::vector<CompFrrForecast> N5cPeers(uint32_t remote, uint64_t excluded,
                                    const std::string& trigger, DecisionPathSnapshot& paths);
    bool RevalidateN5c(FrequencyDecisionRecord& row, const TaskRuntime& task, State& state);
    InputStartAuditRecord CaptureInputStart(const FrequencyDecisionRecord& row, int64_t timeNs) const;
    ///< Read-only snapshots immediately before mechanism execution, not initialization completion.
    ///< Adapt actual placement, legal inventory, pools, rates and paths.
    Ptr<TaskCoordinator> m_tasks;                     ///< Business lifecycle owner.
    SatelliteRuntimeView& m_topology;                 ///< Shared network view.
    Ptr<FaultModelEngine> m_faults;                   ///< Online epoch producer only.
    PlacementLoadLedger m_loads;                      ///< Live remote assignment/recovery counts.
    CheckpointManager m_manager;                      ///< Sole actual checkpoint mechanism.
    std::unique_ptr<PlacementPolicy> m_placement;      ///< FFP baseline or explicitly injected LRL.
    std::unique_ptr<CompFrrPlacementTracker> m_placementObservation; ///< Read-only resource metrics, also usable by FA-FFP.
    CompFrrPlacementTracker* m_n5c{}; ///< Alias enabled only for N5C; legacy policies never use quota promises.
    CompFrrFrequencyPolicy m_policy;                  ///< Pure production solver.
    InputAdmissionPolicy m_inputAdmission{InputAdmissionPolicy::NONE};
    std::unique_ptr<InputStagingManager> m_optionalInput;
    std::vector<std::pair<InputStartAuditRecord, InputAdmissionDecision>> m_inputAdmissions;
    std::unique_ptr<RecoveryController> m_recovery;   ///< Reused N5A recovery.
    std::map<uint64_t, State> m_states;               ///< Per-primary frequency lifecycle.
    std::vector<FrequencyDecisionRecord> m_decisions; ///< Proposal/resolution audit.
    std::vector<PauseInterval> m_pauses;               ///< Actual committed PAUSE intervals.
    std::set<uint64_t> m_waitingCapacity; ///< OFF waiting for first protection admission.
    std::set<uint64_t> m_pausedCapacity; ///< ON paused only for transient path capacity.
    std::vector<PauseInterval> m_capacityWaits; ///< Not a compute reservation or actual waste.
    EventId m_capacityDrain;
    bool m_finalized{};
    bool m_inputStartAudit{}; ///< Default off; no selector, timers, reservations or flows.
    uint64_t m_inputStartRejected{}; ///< Captured but not physically admitted; excluded from candidates.
    std::vector<InputStartAuditRecord> m_inputStartRecords;
};
} // namespace ns3::protection
#endif
