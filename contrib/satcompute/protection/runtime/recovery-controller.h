/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RECOVERY_CONTROLLER_H
#define SATCOMPUTE_RECOVERY_CONTROLLER_H
#include "../../traffic/local-delivery.h"
#include "../mechanism/checkpoint/checkpoint-manager.h"
#include <filesystem>

namespace ns3::protection
{
/** Causal per-recovery evidence; negative timestamps serialize as empty cells. */
struct RecoverySummary
{
    RecoverySnapshot snapshot;            ///< Frozen physical fault-time checkpoint state.
    FaultDefinition fault;                ///< Observed fault, never a future trace lookup.
    uint32_t primaryNode{};               ///< Immutable original placement.
    std::optional<uint32_t> recoveryNode; ///< Empty if no node accepted.
    std::string path, inputMode, resultMode, terminalState, reason; ///< Executed semantics.
    int64_t estimatedTailNs{-1}, estimatedRedoNs{-1}, acceptedNs{-1}, inputStartedNs{-1},
        inputReceivedNs{-1}, tailStartedNs{-1}, tailReceivedNs{-1}, tailCommitNs{-1},
        computeStartedNs{-1}, catchupNs{-1}, computeCompleteNs{-1}, resultStartedNs{-1},
        resultCompleteNs{-1}, terminalNs{-1}, reservedIdleNs{}; ///< Actual causal event times.
    uint64_t plannedCatchupRedoWu{}, plannedPostCatchupWu{}, plannedTotalRecoveryWu{},
        resultBytes{}, resultTransferId{}, normalProtectionCostNs{}; ///< Work/byte/cost accounting.
    uint64_t actualCatchupRedoWu{}, actualPostCatchupWu{}, actualTotalRecoveryWu{}, recoveryRate{},
        primaryRate{};         ///< Observed WU, with explicit rates for equivalent cost.
    int64_t actualServiceNs{}; ///< Actual service prefix, not wall-clock stage duration.
};

/** One recovery event, including local logical deliveries which have no transfer ID. */
struct RecoveryEvent
{
    uint64_t taskId{}, generation{1}; ///< Owning logical attempt.
    int64_t timeNs{};                 ///< Actual simulator event time.
    std::string event, mode;          ///< Stable event name and optional delivery mode.
    uint64_t bytes{}, transferId{};   ///< Logical bytes; zero transfer ID means no UDP.
};

/** Single-attempt checkpoint recovery using existing compute, storage and network services. */
class RecoveryController : public ProtectionMechanism
{
  public:
    /** Bind optional fixed-mode recovery; no global node immunity is introduced. */
    RecoveryController(Ptr<TaskCoordinator> tasks,
                       SatelliteRuntimeView& topology,
                       CheckpointManager& manager,
                       int64_t stopNs,
                       ProtectionPolicy& policy);
    ~RecoveryController();
    bool Supports(ActionKind kind) const override;
    void Execute(const ProtectionContext& context, const ProtectionAction& action) override;
    bool OnComputeFault(const ProtectionContext& context) override;

    void OnTaskComputeComplete(AttemptKey) override
    {
    }

    void OnTaskTerminal(uint64_t taskId) override;
    /** Close incomplete attempts at simulation end and release all owned resources. */
    void Finalize();
    /** Read-only task-ordered evidence for tests and metrics. */
    std::vector<RecoverySummary> Summaries() const;

    /** Read-only causal event ledger. */
    const std::vector<RecoveryEvent>& Events() const
    {
        return m_events;
    }

    /** Dedicated output; never created for protection off. */
    void WriteMetrics(const std::filesystem::path& directory) const;

  private:
    /** Stable heap-owned attempt and asynchronous resources. */
    struct State
    {
        State(const TaskRuntime& task, RecoverySnapshot snapshot, FaultDefinition fault);
        const TaskRuntime& task;     ///< Coordinator storage is stable and outlives this state.
        ExecutionAttempt attempt;    ///< Sole recovery and terminal guard.
        TaskStateAdapter layout;     ///< Same byte/WU model as primary checkpoints.
        RecoverySummary summary;     ///< Actual recovery evidence.
        Ptr<ComputeService> service; ///< Reserved then executing recovery service.
        uint64_t startWork{}, tailObject{}; ///< Adopted progress and temporary pool identity.
        bool live{true}; ///< Guards every timer/transfer callback after terminalization.
        std::vector<EventId> timers;     ///< Decision, local delivery and merge callbacks.
        std::vector<uint64_t> transfers; ///< Real recovery network history.
    };

    bool Fault(const TaskRuntime& task, const TaskFaultNodeChange& change);
    void OnTask(const TaskEventRecord& event);
    void Decide(State& state);
    bool AcceptAndExecute(State& state, uint32_t node);
    bool Eligible(uint32_t node, const State& state) const;
    bool Reachable(uint32_t source, uint32_t destination) const;
    std::optional<int64_t> Estimate(uint32_t source, uint32_t destination, uint64_t bytes) const;
    void Deliver(State& state,
                 ProtectionTransferKind kind,
                 uint32_t source,
                 uint32_t destination,
                 uint64_t bytes,
                 uint64_t object = 0);
    void TransferTerminal(uint64_t transferId, int64_t timeNs);
    void Received(State& state, ProtectionTransferKind kind, uint64_t bytes, uint64_t transferId);
    void StartCompute(State& state);
    void Started(uint64_t id, uint64_t generation, uint32_t node, int64_t at);
    void Catchup(uint64_t id, uint64_t generation, uint32_t node, int64_t at);
    void Computed(uint64_t id, uint64_t generation, uint32_t node, int64_t at);
    void Fail(State& state, const std::string& reason);
    void Cleanup(State& state);
    void Log(State& state,
             const std::string& event,
             uint64_t bytes = 0,
             uint64_t transfer = 0,
             const std::string& mode = "");
    void Later(State& state, int64_t delay, std::function<void()> callback);
    Ptr<ComputeService> Service(uint32_t node) const;
    Ptr<TaskCoordinator> m_tasks;         ///< Existing coordinator.
    SatelliteRuntimeView& m_topology;     ///< Current routes only.
    CheckpointManager& m_manager;         ///< Shared checkpoint ledgers and ID allocator.
    Ptr<NetworkTransferEngine> m_network; ///< Existing real UDP engine.
    int64_t m_stopNs;                     ///< Absolute simulation endpoint.
    ProtectionRuntime m_faultRuntime;     ///< Established mechanism first, then policy fallback.
    std::map<uint64_t, std::unique_ptr<State>> m_states; ///< Sole recovery per task.
    std::map<uint64_t, std::pair<uint64_t, ProtectionTransferKind>> m_flows; ///< Real callbacks.
    std::vector<RecoveryEvent> m_events; ///< Append-only causal history.
};
} // namespace ns3::protection
#endif
