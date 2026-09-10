/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FREQUENCY_PROTECTION_CONTROLLER_H
#define SATCOMPUTE_FREQUENCY_PROTECTION_CONTROLLER_H
#include "../../fault/runtime/fault-model-engine.h"
#include "../policy/baseline/first-feasible-placement/first-feasible-placement-policy.h"
#include "../policy/compfrr/frequency/frequency-decision-gate.h"
#include "frequency-storage-estimator.h"
#include "recovery-controller.h"

#include <filesystem>

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
};

/** Online generate integration. Owns no fault model, RNG, state bytes or second network. */
class FrequencyProtectionController : public ProtectionPolicy
{
  public:
    /** Connect real epoch boundaries, existing checkpoint engine and single-attempt recovery. */
    FrequencyProtectionController(Ptr<TaskCoordinator> tasks,
                                  SatelliteRuntimeView& topology,
                                  Ptr<FaultModelEngine> faults,
                                  uint64_t capacity,
                                  int64_t stopNs);
    ~FrequencyProtectionController() override;
    /** Finish the same actual ledgers as fixed protection. */
    void Finalize();
    /** Write only decision/prediction audit, not actual metrics. */
    void WriteDecisions(const std::filesystem::path& directory) const;

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
    };

    struct Path
    {
        double bytesPerSecond{}, propagationSeconds{}; ///< Current bottleneck and path delay.
        bool local{}; ///< Source equals destination: no real network transfer.
        double Seconds(uint64_t bytes) const; ///< Serialization plus current propagation.
    };

    void OnTask(const TaskEventRecord& event); ///< Task start registers only; no decision timer.
    void BeforeEpoch(const FaultEpochInput& epoch); ///< Pure proposal before random draws.
    void AfterEpoch(int64_t timeNs, const std::vector<FaultEpochOutcome>& outcomes);
    ///< Sole post-fault decision application boundary.
    void Initialized(uint64_t taskId);                ///< Real physical initialization callback.
    const TaskRuntime& Task(uint64_t taskId) const;   ///< Stable logical task lookup.
    Ptr<ComputeService> Service(uint32_t node) const; ///< Actual compute profile owner.
    std::vector<BackupCandidate> Candidates(uint32_t primary) const; ///< Causal FFP inputs.
    std::optional<Path> EstimatePath(uint32_t source, uint32_t destination) const;
    ///< Current deterministic route, never future queue completion.
    bool BuildResources(FrequencyDecisionRecord& row, const TaskRuntime& task, State& state);
    ///< Adapt actual placement, legal inventory, pools, rates and paths.
    Ptr<TaskCoordinator> m_tasks;                     ///< Business lifecycle owner.
    SatelliteRuntimeView& m_topology;                 ///< Shared network view.
    Ptr<FaultModelEngine> m_faults;                   ///< Online epoch producer only.
    CheckpointManager m_manager;                      ///< Sole actual checkpoint mechanism.
    FirstFeasiblePlacementPolicy m_placement;         ///< Fixed N5B placement baseline.
    CompFrrFrequencyPolicy m_policy;                  ///< Pure production solver.
    std::unique_ptr<RecoveryController> m_recovery;   ///< Reused N5A recovery.
    std::map<uint64_t, State> m_states;               ///< Per-primary frequency lifecycle.
    std::vector<FrequencyDecisionRecord> m_decisions; ///< Proposal/resolution audit.
};
} // namespace ns3::protection
#endif
