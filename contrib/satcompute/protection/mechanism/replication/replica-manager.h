/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_REPLICA_MANAGER_H
#define SATCOMPUTE_REPLICA_MANAGER_H
#include "../checkpoint/checkpoint-manager.h"
#include "../../policy/baseline/one-plus-one/one-plus-one-policy.h"
#include "../../../traffic/local-delivery.h"
#include <array>

namespace ns3::protection
{
/** Physical attempt stage, independent of the aggregate logical task state. */
enum class ReplicaStage { ABSENT, INPUT, READY, RUNNING, RESULT, COMPLETED, FAILED, CANCELLED };
const char* ReplicaStageName(ReplicaStage stage);

/** Actual attempt evidence retained even for a losing or failed attempt. */
struct ReplicaAttempt
{
    uint64_t generation{}; ///< Primary zero, sole replica one.
    uint32_t node{}; ///< Real ComputeService node.
    ReplicaStage stage{ReplicaStage::ABSENT};
    uint64_t rate{}, actualWork{}, actualServiceNs{}, inputTransfer{}, resultTransfer{};
    int64_t reservedNs{-1}, plannedInputWaitNs{-1}, inputStartedNs{-1}, inputReceivedNs{-1},
        computeStartedNs{-1}, computeCompleteNs{-1}, resultStartedNs{-1}, resultCompleteNs{-1},
        terminalNs{-1}, takeoverNs{-1}, reservedIdleNs{};
    bool faulted{}, immune{}; ///< Normal replicas are not immune; takeover is batch-ordered.
    std::string inputMode, resultMode, reason;
};

struct ReplicaSummary
{
    uint64_t taskId{}, totalWork{}, inputBytes{}, resultBytes{};
    int64_t requestedNs{-1}, deadlineNs{-1}, terminalNs{-1};
    bool requested{}, admitted{};
    std::string admissionReason, winner, terminalState, reason;
    std::array<ReplicaAttempt, 2> attempts; ///< Both histories, never overwritten by the winner.
};

struct ReplicaEvent
{
    uint64_t taskId{}, generation{};
    uint32_t node{};
    int64_t timeNs{};
    std::string event;
    uint64_t bytes{}, transferId{};
};

/** Real full replica execution and batch-safe logical winner arbitration. */
class ReplicaManager
{
  public:
    ReplicaManager(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                   int64_t stopNs, OnePlusOnePolicy& policy);
    ~ReplicaManager();
    /** One primary TASK_RUNNING: request once, without delaying its service. */
    void Request(uint64_t taskId);
    /** Close every attempt and unused original RESULT at simulation end. */
    void Finalize();
    std::vector<ReplicaSummary> Summaries() const;
    const std::vector<ReplicaEvent>& Events() const { return m_events; }
    const CheckpointManager& Ledger() const { return m_ledger; }
    /** Per-attempt, per-task and physical transfer evidence. */
    void WriteMetrics(const std::filesystem::path& directory) const;

  private:
    struct State
    {
        explicit State(const TaskRuntime& runtime) : task(runtime) {}
        const TaskRuntime& task;
        ReplicaSummary summary;
        bool live{true};
        std::vector<EventId> timers;
    };
    bool Valid(const ReplicaAttempt& attempt) const;
    Ptr<ComputeService> Service(uint32_t node) const;
    void Log(State& state, uint64_t generation, const std::string& event,
             uint64_t bytes = 0, uint64_t transfer = 0);
    void UpdateActual(State& state, ReplicaAttempt& attempt);
    void PrimaryComputed(uint64_t id, uint32_t node, int64_t at);
    void ReplicaStarted(uint64_t id, uint64_t generation, uint32_t node, int64_t at);
    void ReplicaComputed(uint64_t id, uint64_t generation, uint32_t node, int64_t at);
    void Computed(State& state, uint64_t generation, int64_t at);
    void DeliverReplica(State& state, bool input);
    void StartReplica(State& state);
    void TransferTerminal(uint64_t id, int64_t at);
    void Received(State& state, uint64_t generation, bool input, uint64_t bytes);
    void Win(State& state, uint64_t generation);
    void Cancel(State& state, uint64_t generation, const std::string& reason, bool failed);
    void Fail(State& state, const std::string& reason, TaskFailureReason failure);
    void Deadline(uint64_t id);
    std::map<uint32_t, TaskFaultImpact> FaultBatch(const std::vector<TaskFaultNodeChange>& changes);
    void BatchComplete();
    uint64_t ServiceNs(const State& state) const;
    Ptr<TaskCoordinator> m_tasks;
    SatelliteRuntimeView& m_topology;
    Ptr<NetworkTransferEngine> m_network;
    OnePlusOnePolicy& m_policy;
    CheckpointManager m_ledger; ///< Only shared canonical flow registration; zero objects/capacity.
    std::map<uint64_t, std::unique_ptr<State>> m_states;
    struct Flow { uint64_t taskId{}, generation{}; bool input{}; };
    std::map<uint64_t, Flow> m_flows;
    std::vector<ReplicaEvent> m_events;
    bool m_inFaultBatch{}; ///< Prevents callback-order logical failure/takeover during the batch.
};
} // namespace ns3::protection
#endif
