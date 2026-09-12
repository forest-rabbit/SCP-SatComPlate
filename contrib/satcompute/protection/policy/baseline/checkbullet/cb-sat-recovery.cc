/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-recovery.h"
#include "../fa-first-feasible-placement/fa-first-feasible-placement-policy.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection::checkbullet
{
namespace
{
int64_t Now() { return Simulator::Now().GetNanoSeconds(); }
int64_t Duration(uint64_t work, uint64_t rate)
{
    return work ? ComputeService::CalculateServiceTimeNs(work, rate) : 0;
}
void Require(bool value, const char* text)
{
    if (!value) throw std::logic_error(text);
}
} // namespace

CbSatRecovery::State::State(const TaskRuntime& runtime, CbRecoverySnapshot snapshot,
                            const FaultDefinition& fault)
    : task(runtime), layout(runtime.definition),
      attempt(runtime.definition.taskId, runtime.definition.computeNodeId, runtime.computeDeadlineTimeNs)
{
    summary.task = runtime.definition.taskId;
    summary.primary = runtime.definition.computeNodeId;
    summary.snapshot = std::move(snapshot);
    summary.fault = fault;
    summary.resultBytes = runtime.definition.outputBytes;
}

CbSatRecovery::CbSatRecovery(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                              CbSatManager& manager, PlacementLoadLedger& loads,
                              int64_t stopNs, RemoteBusyRecoveryPolicy busyPolicy)
    : m_tasks(tasks), m_topology(topology), m_manager(manager), m_loads(loads),
      m_network(tasks->GetTransferEngine()), m_stopNs(stopNs), m_busyPolicy(busyPolicy)
{
    tasks->SetRecoveryHandler([this](const auto& task, const auto& change) { return Fault(task, change); });
    tasks->ConnectTaskObserver(MakeCallback(&CbSatRecovery::OnTask, this));
}

CbSatRecovery::~CbSatRecovery()
{
    m_tasks->SetRecoveryHandler({});
    m_tasks->DisconnectTaskObserver(MakeCallback(&CbSatRecovery::OnTask, this));
    for (auto& [id, state] : m_states)
        for (auto timer : state->timers) Simulator::Cancel(timer);
}

Ptr<ComputeService> CbSatRecovery::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node) return service;
    return nullptr;
}

void CbSatRecovery::Later(State& state, int64_t delay, std::function<void()> callback)
{
    Require(delay >= 0, "negative CB recovery delay");
    if (delay >= m_stopNs - Now()) return;
    state.timers.push_back(Simulator::Schedule(NanoSeconds(delay),
        [&state, callback = std::move(callback)] { if (state.live) callback(); }));
}

void CbSatRecovery::Log(State& state, const std::string& event, uint64_t bytes,
                        uint64_t transfer, const std::string& role)
{
    m_manager.Log(state.summary.task, event, state.summary.node.value_or(state.summary.primary),
        0, 0, state.summary.resumeWork, bytes, transfer, role);
}

bool CbSatRecovery::Fault(const TaskRuntime& task, const TaskFaultNodeChange& change)
{
    if (change.kind == TaskFaultKind::SATELLITE) m_manager.InvalidateNode(change.nodeId);
    if (task.attemptGeneration)
    {
        const auto found = m_states.find(task.definition.taskId);
        if (found == m_states.end() || !found->second->live) return false;
        auto& state = *found->second;
        auto& r = state.summary;
        if (change.kind == TaskFaultKind::COMPUTE)
        {
            if (r.node == change.nodeId && state.attempt.ImmuneToComputeFault())
                Log(state, "RECOVERY_COMPUTE_FAULT_IGNORED");
            return false;
        }
        const bool inputLost = r.inputReadyNs < 0 && state.inputSource == change.nodeId;
        const bool movingLost = r.path == "RELOCATE" && r.stateReceivedNs < 0 &&
                                r.snapshot.state.backupNode == change.nodeId;
        if (r.node == change.nodeId || task.definition.resultNodeId == change.nodeId ||
            inputLost || movingLost)
        {
            Fail(state, "RECOVERY_F3_SATELLITE_FAILURE");
            return true;
        }
        return false;
    }
    if (task.state != TASK_RUNNING || task.definition.computeNodeId != change.nodeId) return false;
    auto snapshot = m_manager.Freeze(task.definition.taskId, Now());
    auto owned = std::make_unique<State>(task, snapshot, change.fault);
    auto& state = *owned;
    state.summary.primaryRate = Service(task.definition.computeNodeId)->GetComputeRateWorkUnitsPerSecond();
    Require(m_states.emplace(task.definition.taskId, std::move(owned)).second,
            "second CB recovery attempt forbidden");
    Log(state, "CB_FAULT_SNAPSHOT");
    m_manager.RetainForRecovery(snapshot);
    Require(m_tasks->BeginRecovery(task.definition.taskId), "CB primary handoff rejected");
    // All same-ns availability changes AND the topology overlay complete before this decision.
    Later(state, 1, [this, &state] { Decide(state); });
    return true;
}

bool CbSatRecovery::Reachable(uint32_t source, uint32_t destination) const
{
    return m_tasks->IsSatelliteAvailable(source) && m_tasks->IsSatelliteAvailable(destination) &&
           (source == destination || !m_topology.GetEcmpRouteCandidates(source, destination).empty());
}

std::optional<int64_t> CbSatRecovery::Estimate(uint32_t source, uint32_t destination, uint64_t bytes) const
{
    if (!Reachable(source, destination)) return std::nullopt;
    return m_network->EstimateAdmissiblePath(source, destination).TransferTimeNs(bytes);
}

bool CbSatRecovery::Eligible(const State& state, uint32_t node) const
{
    const auto service = Service(node);
    return node != state.summary.primary && service && m_tasks->IsSatelliteAvailable(node) &&
           m_tasks->IsComputeAvailable(node) && service->IsIdle() &&
           Reachable(node, state.task.definition.resultNodeId);
}

bool CbSatRecovery::HasInput(const State& state) const
{
    const auto& snapshot = state.summary.snapshot;
    if (!snapshot.state.inputReady || !snapshot.inputObject ||
        !m_tasks->IsSatelliteAvailable(snapshot.state.backupNode)) return false;
    const auto entry = m_manager.Pools().at(snapshot.state.backupNode)->Find(snapshot.inputObject);
    return entry && !entry->reserved && entry->taskId == state.summary.task &&
           entry->bytes == state.task.definition.inputBytes;
}

bool CbSatRecovery::HasState(const State& state) const
{
    const auto& snapshot = state.summary.snapshot;
    if (!snapshot.state.rootReady || !snapshot.rootObject ||
        !m_tasks->IsSatelliteAvailable(snapshot.state.backupNode)) return false;
    const auto& pool = *m_manager.Pools().at(snapshot.state.backupNode);
    const auto root = pool.Find(snapshot.rootObject);
    const auto model = m_manager.State(state.summary.task);
    if (!root || root->reserved || root->taskId != state.summary.task || !model ||
        root->bytes != model->FullBytes(snapshot.state.rootWork)) return false;
    for (const auto& [sequence, object] : snapshot.logObjects)
    {
        const auto log = pool.Find(object);
        if (!log || log->reserved || log->taskId != state.summary.task ||
            log->bytes != model->Records().at(sequence).bytes) return false;
    }
    return snapshot.logObjects.size() == snapshot.state.logSequences.size();
}

std::vector<uint32_t> CbSatRecovery::Candidates(const State& state) const
{
    PlacementContext context{state.summary.primary, {}};
    for (auto service : m_tasks->GetComputeServices())
    {
        const auto node = service->GetNodeId();
        context.candidates.push_back({node,
            m_tasks->IsComputeAvailable(node) && m_tasks->IsSatelliteAvailable(node),
            service->IsIdle(), Reachable(node, state.task.definition.resultNodeId)});
    }
    auto nodes = BuildFeasibleBackupNodes(context);
    // This is the frozen checkpoint-recovery ranking, not native R0 or future N5C placement.
    FaFirstFeasiblePlacementPolicy{}.RankBackupNodes(nodes, context);
    return nodes;
}

void CbSatRecovery::Decide(State& state)
{
    auto& r = state.summary;
    const auto& snapshot = r.snapshot;
    const auto backup = snapshot.state.backupNode;
    if (Now() >= snapshot.deadlineNs) return Fail(state, "DEADLINE_BEFORE_RECOVERY_ACCEPTANCE");
    if (!m_tasks->IsSatelliteAvailable(state.task.definition.resultNodeId))
        return Fail(state, "RESULT_SATELLITE_UNAVAILABLE");
    const bool saved = HasState(state);
    if (snapshot.rootObject && !m_tasks->IsSatelliteAvailable(backup)) r.fallbackReason = "REMOTE_F3";
    else if (!saved) r.fallbackReason = "STATE_MISSING";
    else if (!m_tasks->IsComputeAvailable(backup)) r.fallbackReason = "REMOTE_UNAVAILABLE";
    else if (!Service(backup)->IsIdle()) r.fallbackReason = "REMOTE_BUSY";
    else if (!Reachable(backup, state.task.definition.resultNodeId)) r.fallbackReason = "PATH_UNAVAILABLE";
    else r.fallbackReason = "INPUT_OR_STORAGE_UNAVAILABLE";
    r.remoteBusy = saved && r.fallbackReason == "REMOTE_BUSY";
    if (saved && Eligible(state, backup) && Accept(state, backup, "DIRECT"))
    {
        r.fallbackReason.clear();
        return;
    }
    if (saved && AllowsCheckpointRelocation(m_busyPolicy, r.fallbackReason) && TryRelocate(state)) return;
    // An INPUT-only initialization is not a nonzero-progress checkpoint, but its real local
    // original input may still be used for from-zero execution on that same healthy holder.
    if (!saved && HasInput(state) && Eligible(state, backup) && Accept(state, backup, "RECOMPUTE")) return;
    for (auto node : Candidates(state))
        if (Eligible(state, node) && Reachable(state.task.definition.sourceNodeId, node) &&
            Accept(state, node, "RECOMPUTE")) return;
    Fail(state, "NO_ELIGIBLE_RECOVERY_NODE_OR_INPUT");
}

bool CbSatRecovery::TryRelocate(State& state)
{
    auto& r = state.summary;
    const auto& snapshot = r.snapshot;
    const auto backup = snapshot.state.backupNode;
    r.relocationAttempted = true;
    r.relocationFailure = "NO_ELIGIBLE_RECOVERY_NODE";
    const auto model = m_manager.State(r.task);
    uint64_t stateBytes = model->FullBytes(snapshot.state.rootWork);
    for (auto sequence : snapshot.state.logSequences) stateBytes += model->Records().at(sequence).bytes;
    for (auto node : Candidates(state))
    {
        if (node == backup || !Eligible(state, node)) continue;
        const auto total = stateBytes + state.task.definition.inputBytes;
        if (total > m_manager.Quota(node, r.task) - m_manager.Occupied(node, r.task))
        {
            r.relocationFailure = "DESTINATION_STORAGE_UNAVAILABLE";
            continue;
        }
        const auto transfer = Estimate(backup, node, stateBytes +
            (HasInput(state) ? state.task.definition.inputBytes : 0));
        const auto input = HasInput(state) ? std::optional<int64_t>{0} :
            Estimate(state.task.definition.sourceNodeId, node, state.task.definition.inputBytes);
        if (!transfer || !input)
        {
            r.relocationFailure = "NO_ADMISSIBLE_PATH";
            continue;
        }
        const auto rate = Service(node)->GetComputeRateWorkUnitsPerSecond();
        const auto restore = snapshot.state.logSequences.empty() ? 0 :
            GetProtectionCosts(state.layout.VariableBytes()).remoteNs;
        const auto work = Duration(state.layout.Work() - snapshot.state.recoverableWork, rate);
        if (std::max(*transfer, *input) + restore + work > snapshot.deadlineNs - Now())
        {
            r.relocationFailure = "CHECKPOINT_DEADLINE_INFEASIBLE";
            continue;
        }
        if (Accept(state, node, "RELOCATE"))
        {
            r.relocationFailure.clear();
            return true;
        }
    }
    return false;
}

bool CbSatRecovery::Accept(State& state, uint32_t node, const std::string& path)
{
    auto& r = state.summary;
    const auto& snapshot = r.snapshot;
    const auto backup = snapshot.state.backupNode;
    const bool checkpoint = path != "RECOMPUTE";
    const bool inputHere = node == backup && HasInput(state);
    const bool movingInput = path == "RELOCATE" && HasInput(state);
    const auto inputSource = movingInput ? backup : state.task.definition.sourceNodeId;
    if (!inputHere && (!Reachable(inputSource, node) ||
        (checkpoint && !Estimate(inputSource, node, state.task.definition.inputBytes)))) return false;
    std::vector<uint64_t> created;
    auto reserve = [&](const char* role, uint64_t bytes) -> uint64_t {
        const auto object = m_manager.Reserve(r.task, node, role, bytes);
        if (object) created.push_back(*object);
        return object.value_or(0);
    };
    const auto input = inputHere ? snapshot.inputObject : reserve("INPUT", state.task.definition.inputBytes);
    uint64_t root = checkpoint ? snapshot.rootObject : 0;
    std::map<uint64_t, uint64_t> logs = checkpoint ? snapshot.logObjects : std::map<uint64_t, uint64_t>{};
    bool allocated = input != 0;
    if (path == "RELOCATE")
    {
        root = reserve("FULL", m_manager.State(r.task)->FullBytes(snapshot.state.rootWork));
        allocated = allocated && root;
        logs.clear();
        for (auto sequence : snapshot.state.logSequences)
        {
            const auto object = reserve("LOG", m_manager.State(r.task)->Records().at(sequence).bytes);
            allocated = allocated && object;
            logs.emplace(sequence, object);
        }
    }
    auto service = Service(node);
    if (!allocated || !service->ReserveRecovery(r.task, 1))
    {
        for (auto object : created) m_manager.ReleaseObject(node, object, "RECOVERY_ADMISSION_ROLLBACK");
        return false;
    }
    Require(state.attempt.AcceptRecovery(node, true, Now()), "CB attempt acceptance rejected");
    state.service = service;
    state.inputObject = input;
    state.rootObject = root;
    state.logObjects = logs;
    state.storedInput = inputHere;
    state.inputSource = inputHere ? backup : inputSource;
    r.path = path;
    r.node = node;
    r.acceptedNs = Now();
    r.recoveryRate = service->GetComputeRateWorkUnitsPerSecond();
    r.resumeWork = checkpoint ? snapshot.state.recoverableWork : 0;
    r.plannedCatchupWu = snapshot.actualWork - r.resumeWork;
    r.plannedRemainingWu = state.layout.Work() - snapshot.actualWork;
    m_loads.Recovery(r.task, node, true, Now());
    Log(state, "RECOVERY_ACCEPTED");
    if (inputHere)
    {
        r.inputReadyNs = Now();
        r.inputMode = "STORED_LOCAL";
        Log(state, "RECOVERY_INPUT_ALREADY_STORED", state.task.definition.inputBytes, 0, r.inputMode);
    }
    else
        Deliver(state, movingInput ? CbFlowKind::RELOCATE_INPUT : CbFlowKind::FALLBACK_INPUT,
            0, inputSource, node, state.task.definition.inputBytes, input);
    if (!state.live) return true;
    if (path == "RELOCATE")
    {
        r.stateStartedNs = Now();
        r.relocationBytes = (movingInput ? state.task.definition.inputBytes : 0) +
            m_manager.State(r.task)->FullBytes(snapshot.state.rootWork);
        Deliver(state, CbFlowKind::RELOCATE_FULL, snapshot.state.rootSequence, backup, node,
                m_manager.State(r.task)->FullBytes(snapshot.state.rootWork), root);
        for (auto sequence : snapshot.state.logSequences)
        {
            if (!state.live) break;
            const auto bytes = m_manager.State(r.task)->Records().at(sequence).bytes;
            r.relocationBytes += bytes;
            Deliver(state, CbFlowKind::RELOCATE_LOG, sequence, backup, node, bytes, logs.at(sequence));
        }
    }
    else
        r.stateReceivedNs = Now();
    if (state.live) Ready(state);
    return true;
}

void CbSatRecovery::Deliver(State& state, CbFlowKind kind, uint64_t sequence,
                            uint32_t source, uint32_t destination, uint64_t bytes, uint64_t object)
{
    if (!Reachable(source, destination)) return Fail(state, "RECOVERY_DELIVERY_UNREACHABLE");
    const auto mode = bytes == 0 ? "ZERO_BYTES" : source == destination ? "LOCAL" : "NETWORK";
    auto& r = state.summary;
    if (kind == CbFlowKind::FALLBACK_INPUT || kind == CbFlowKind::RELOCATE_INPUT)
    {
        Require(r.inputStartedNs < 0, "CB recovery INPUT requested more than once");
        r.inputStartedNs = Now();
        r.inputMode = mode;
    }
    if (kind == CbFlowKind::RESULT)
    {
        r.resultStartedNs = Now();
        r.resultMode = mode;
    }
    Require(state.pending.emplace(kind, sequence).second, "duplicate CB recovery object delivery");
    Log(state, std::string(CbFlowName(kind)) + "_STARTED", bytes, 0, mode);
    if (!bytes)
    {
        Later(state, 1, [this, &state, kind, sequence, object] {
            Received(state, kind, sequence, object, 0, 0);
        });
        return;
    }
    if (source == destination)
    {
        state.timers.push_back(LocalDelivery::Schedule(source, destination, bytes,
            [this, &state, source] {
                return state.live && state.attempt.Owns({state.summary.task, 1}) &&
                       m_tasks->IsSatelliteAvailable(source);
            },
            [this, &state, kind, sequence, object](auto delivered, int64_t) {
                Received(state, kind, sequence, object, delivered, 0);
            }));
        return;
    }
    m_manager.Queue(r.task, 1, kind, sequence, source, destination, bytes, object,
        [&state] { return state.live && state.attempt.Owns({state.summary.task, 1}); },
        [this, &state, kind, sequence, object, bytes](auto transfer, bool completed) {
            if (!completed) return Fail(state, "RECOVERY_TRANSFER_FAILED");
            Require(m_network->GetReceivedBytes(transfer) == bytes, "CB receiver size mismatch");
            Received(state, kind, sequence, object, bytes, transfer);
        });
}

void CbSatRecovery::Received(State& state, CbFlowKind kind, uint64_t sequence,
                             uint64_t object, uint64_t bytes, uint64_t transfer)
{
    if (!state.live || !state.attempt.Owns({state.summary.task, 1})) return;
    Require(state.pending.erase({kind, sequence}) == 1, "duplicate CB recovery completion");
    auto& r = state.summary;
    if (object) m_manager.CommitObject(*r.node, object);
    Log(state, std::string(CbFlowName(kind)) + "_RECEIVED", bytes, transfer);
    if (kind == CbFlowKind::RESULT)
    {
        Require(bytes == r.resultBytes, "CB RESULT byte mismatch");
        r.resultNs = Now(); r.resultTransfer = transfer;
        Require(state.attempt.CompleteResult({r.task, 1}), "CB RESULT attempt rejected");
        Require(m_tasks->RecoveryResult(r.task, 1, transfer, !transfer), "CB logical RESULT rejected");
        return;
    }
    if (kind == CbFlowKind::FALLBACK_INPUT || kind == CbFlowKind::RELOCATE_INPUT)
        r.inputReadyNs = Now();
    Ready(state);
}

void CbSatRecovery::Ready(State& state)
{
    auto& r = state.summary;
    if (!state.live || r.computeStartedNs >= 0) return;
    if (!state.restoreScheduled)
    {
        const bool receiving = std::any_of(state.pending.begin(), state.pending.end(), [](const auto& key) {
            return key.first == CbFlowKind::RELOCATE_FULL || key.first == CbFlowKind::RELOCATE_LOG;
        });
        if (receiving) return;
        r.stateReceivedNs = Now();
        state.restoreScheduled = true;
        const auto cost = r.path == "RECOMPUTE" || r.snapshot.state.logSequences.empty() ? 0 :
            GetProtectionCosts(state.layout.VariableBytes()).remoteNs;
        auto apply = [this, &state, cost] {
            auto& r = state.summary;
            if (r.path != "RECOMPUTE" &&
                !m_manager.ApplyStoredLogs(r.snapshot, *r.node, state.rootObject, state.logObjects))
                return Fail(state, "INVALID_FROZEN_CHECKPOINT_PLAN");
            r.restoreProcessingNs = cost; // Diagnostic only: already inside real reserved-idle time.
            r.stateReadyNs = Now();
            Log(state, "CB_STATE_READY");
            StartCompute(state);
        };
        if (cost)
        {
            r.restoreStartedNs = Now();
            Log(state, "CB_RESTORE_START");
            Later(state, cost, apply);
        }
        else apply();
    }
    StartCompute(state);
}

void CbSatRecovery::StartCompute(State& state)
{
    auto& r = state.summary;
    if (!state.live || r.computeStartedNs >= 0 || r.inputReadyNs < 0 || r.stateReadyNs < 0) return;
    if (!state.attempt.StartRecovery({r.task, 1}, Now())) return Fail(state, "RECOVERY_COMPUTE_DEADLINE");
    Require(Now() >= std::max(r.stateReadyNs, r.inputReadyNs), "CB compute preceded dependencies");
    Log(state, "CB_WORKING_STATE_ADOPTED");
    m_manager.ReleaseTask(r.task, "RECOVERY_WORKING_STATE_ADOPTED");
    if (!state.service->StartRecovery(r.task, 1, state.layout.Work() - r.resumeWork, r.plannedCatchupWu,
        MakeCallback(&CbSatRecovery::Started, this), MakeCallback(&CbSatRecovery::Catchup, this),
        MakeCallback(&CbSatRecovery::Computed, this))) Fail(state, "RECOVERY_COMPUTE_SERVICE_REJECTED");
}

void CbSatRecovery::Started(uint64_t id, uint64_t generation, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, generation})) return;
    Require(m_tasks->RecoveryStarted(id, generation, node), "CB service start rejected");
    state.summary.computeStartedNs = at;
    state.summary.reservedIdleNs = at - state.summary.acceptedNs;
    Log(state, "RECOVERY_COMPUTE_STARTED");
}

void CbSatRecovery::Catchup(uint64_t id, uint64_t generation, uint32_t, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, generation})) return;
    state.summary.catchupNs = at;
    Log(state, "CATCHUP_REACHED");
}

void CbSatRecovery::Computed(uint64_t id, uint64_t generation, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, generation})) return;
    if (!state.attempt.CompleteCompute({id, generation}, at)) return Fail(state, "RECOVERY_COMPUTE_DEADLINE");
    state.summary.computedNs = at;
    m_loads.Recovery(id, node, false, at);
    Log(state, "RECOVERY_COMPUTE_COMPLETE");
    Require(m_tasks->RecoveryComputed(id, generation, at - state.summary.computeStartedNs),
            "CB compute completion rejected");
    Deliver(state, CbFlowKind::RESULT, 0, node, state.task.definition.resultNodeId,
            state.task.definition.outputBytes);
}

void CbSatRecovery::Fail(State& state, const std::string& reason)
{
    if (!state.live) return;
    state.summary.reason = reason;
    if (state.summary.path == "RELOCATE" && state.summary.computeStartedNs < 0)
        state.summary.relocationFailure = reason;
    state.attempt.Fail();
    Log(state, "CB_RECOVERY_FAILED");
    m_tasks->FailRecovery(state.summary.task, reason, reason == "SIMULATION_ENDED" ?
        TaskFailureReason::SIMULATION_ENDED : TaskFailureReason::COMPUTE_NODE_FAILURE);
}

void CbSatRecovery::OnTask(const TaskEventRecord& event)
{
    const auto found = m_states.find(event.taskId);
    if (!IsTerminalTaskState(event.toState) || found == m_states.end()) return;
    auto& state = *found->second;
    auto& r = state.summary;
    r.terminal = TaskStateToString(event.toState);
    r.terminalNs = event.simulationTimeNs;
    if (r.reason.empty()) r.reason = event.cause;
    if (event.toState == TASK_FAILED)
    {
        if (r.path == "RELOCATE" && r.computeStartedNs < 0) r.relocationFailure = r.reason;
        if (state.attempt.Fail()) Log(state, "CB_RECOVERY_FAILED");
    }
    Cleanup(state);
}

void CbSatRecovery::Cleanup(State& state)
{
    if (!state.live) return;
    state.live = false;
    auto& r = state.summary;
    for (auto event : state.timers) Simulator::Cancel(event);
    if (r.node) m_loads.Recovery(r.task, *r.node, false, Now());
    if (state.service)
    {
        if (r.computeStartedNs < 0 && r.acceptedNs >= 0) r.reservedIdleNs = Now() - r.acceptedNs;
        state.service->CancelRecovery(r.task, 1);
    }
    m_manager.CancelFlows(r.task, 1);
    m_manager.ReleaseTask(r.task, "RECOVERY_TERMINAL");
    Log(state, "CB_RECOVERY_CLEANUP");
}

void CbSatRecovery::Finalize()
{
    for (auto& [id, state] : m_states) if (state->live) Fail(*state, "SIMULATION_ENDED");
}

std::vector<CbRecoverySummary> CbSatRecovery::Summaries() const
{
    std::vector<CbRecoverySummary> out;
    for (const auto& [id, state] : m_states)
    {
        auto row = state->summary;
        if (state->service)
        {
            const auto actual = state->service->GetRecoveryAccounting(id, 1);
            row.actualServiceNs = actual.serviceNs;
            row.actualRecoveryWu = actual.executedWork;
            row.actualCatchupWu = std::min(actual.executedWork, row.plannedCatchupWu);
            row.actualRemainingWu = actual.executedWork - row.actualCatchupWu;
        }
        out.push_back(row);
    }
    return out;
}
} // namespace ns3::protection::checkbullet
