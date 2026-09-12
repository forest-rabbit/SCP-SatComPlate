/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CB_SAT_RECOVERY_H
#define SATCOMPUTE_CB_SAT_RECOVERY_H
#include "cb-sat-manager.h"
#include "../../../policy/recovery-policy.h"
#include "../../../../traffic/local-delivery.h"

namespace ns3::protection::checkbullet
{
/** CB recovery evidence, deliberately not represented as a double-tier snapshot. */
struct CbRecoverySummary
{
    uint64_t task{}, resumeWork{}, plannedCatchupWu{}, plannedRemainingWu{}, actualCatchupWu{},
        actualRemainingWu{}, actualRecoveryWu{}, recoveryRate{}, primaryRate{}, relocationBytes{};
    uint32_t primary{};
    std::optional<uint32_t> node;
    CbRecoverySnapshot snapshot;
    FaultDefinition fault;
    int64_t acceptedNs{-1}, inputStartedNs{-1}, inputReadyNs{-1}, stateStartedNs{-1},
        stateReceivedNs{-1}, restoreStartedNs{-1}, stateReadyNs{-1}, computeStartedNs{-1},
        catchupNs{-1}, computedNs{-1}, resultStartedNs{-1}, resultNs{-1}, terminalNs{-1},
        reservedIdleNs{}, actualServiceNs{}, restoreProcessingNs{};
    uint64_t resultBytes{}, resultTransfer{};
    std::string path, fallbackReason, relocationFailure, reason, terminal, inputMode, resultMode;
    bool relocationAttempted{}, remoteBusy{};
};

/** One CB-owned recovery attempt; only explicitly frozen B objects can restore progress. */
class CbSatRecovery
{
  public:
    CbSatRecovery(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                  CbSatManager& manager, PlacementLoadLedger& loads,
                  int64_t stopNs, RemoteBusyRecoveryPolicy busyPolicy);
    ~CbSatRecovery();
    void Finalize(); ///< End incomplete attempts without inventing completed service.
    std::vector<CbRecoverySummary> Summaries() const;

  private:
    struct State
    {
        State(const TaskRuntime& task, CbRecoverySnapshot snapshot, const FaultDefinition& fault);
        const TaskRuntime& task;
        TaskStateAdapter layout;
        ExecutionAttempt attempt;
        CbRecoverySummary summary;
        Ptr<ComputeService> service;
        bool live{true}, restoreScheduled{}, storedInput{};
        std::optional<uint32_t> inputSource;
        uint64_t inputObject{}, rootObject{};
        std::map<uint64_t, uint64_t> logObjects;
        std::set<std::pair<CbFlowKind, uint64_t>> pending;
        std::vector<EventId> timers;
    };
    bool Fault(const TaskRuntime& task, const TaskFaultNodeChange& change);
    void OnTask(const TaskEventRecord& event);
    void Decide(State& state);
    bool HasInput(const State& state) const;
    bool HasState(const State& state) const;
    bool Eligible(const State& state, uint32_t node) const;
    bool Reachable(uint32_t source, uint32_t destination) const;
    std::optional<int64_t> Estimate(uint32_t source, uint32_t destination, uint64_t bytes) const;
    std::vector<uint32_t> Candidates(const State& state) const;
    bool TryRelocate(State& state);
    bool Accept(State& state, uint32_t node, const std::string& path);
    void Deliver(State& state, CbFlowKind kind, uint64_t sequence,
                  uint32_t source, uint32_t destination, uint64_t bytes, uint64_t object = 0);
    void Received(State& state, CbFlowKind kind, uint64_t sequence, uint64_t object,
                   uint64_t bytes, uint64_t transfer);
    void Ready(State& state);
    void StartCompute(State& state);
    void Started(uint64_t task, uint64_t generation, uint32_t node, int64_t at);
    void Catchup(uint64_t task, uint64_t generation, uint32_t node, int64_t at);
    void Computed(uint64_t task, uint64_t generation, uint32_t node, int64_t at);
    void Later(State& state, int64_t delay, std::function<void()> callback);
    void Fail(State& state, const std::string& reason);
    void Cleanup(State& state);
    void Log(State& state, const std::string& event, uint64_t bytes = 0,
              uint64_t transfer = 0, const std::string& role = "");
    Ptr<ComputeService> Service(uint32_t node) const;
    Ptr<TaskCoordinator> m_tasks;
    SatelliteRuntimeView& m_topology;
    CbSatManager& m_manager;
    PlacementLoadLedger& m_loads;
    Ptr<NetworkTransferEngine> m_network;
    int64_t m_stopNs;
    RemoteBusyRecoveryPolicy m_busyPolicy;
    std::map<uint64_t, std::unique_ptr<State>> m_states;
};
} // namespace ns3::protection::checkbullet
#endif
