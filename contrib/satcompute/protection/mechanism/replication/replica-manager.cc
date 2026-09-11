/* SPDX-License-Identifier: GPL-2.0-only */
#include "replica-manager.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
int64_t Now() { return Simulator::Now().GetNanoSeconds(); }
void Require(bool value, const char* message)
{
    if (!value) throw std::logic_error(message);
}
} // namespace

const char* ReplicaStageName(ReplicaStage stage)
{
    switch (stage)
    {
    case ReplicaStage::ABSENT: return "ABSENT";
    case ReplicaStage::INPUT: return "INPUT";
    case ReplicaStage::READY: return "READY";
    case ReplicaStage::RUNNING: return "RUNNING";
    case ReplicaStage::RESULT: return "RESULT";
    case ReplicaStage::COMPLETED: return "COMPLETED";
    case ReplicaStage::FAILED: return "FAILED";
    case ReplicaStage::CANCELLED: return "CANCELLED";
    }
    throw std::logic_error("unknown replica attempt stage");
}

ReplicaManager::ReplicaManager(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
    int64_t stopNs, OnePlusOnePolicy& policy)
    : m_tasks(tasks), m_topology(topology), m_network(tasks->GetTransferEngine()),
      m_policy(policy), m_ledger(tasks, topology, 0, stopNs)
{
    m_tasks->SetParallelAttemptHooks({
        [this](auto id, auto node, auto at) { PrimaryComputed(id, node, at); },
        [this](auto id) { Deadline(id); },
        [this](const auto& changes) { return FaultBatch(changes); },
        [this] { BatchComplete(); }});
}

ReplicaManager::~ReplicaManager()
{
    m_tasks->SetParallelAttemptHooks({});
    for (auto& [id, state] : m_states)
        for (auto event : state->timers) Simulator::Cancel(event);
}

bool ReplicaManager::Valid(const ReplicaAttempt& attempt) const
{
    return attempt.stage == ReplicaStage::INPUT || attempt.stage == ReplicaStage::READY ||
           attempt.stage == ReplicaStage::RUNNING || attempt.stage == ReplicaStage::RESULT;
}

Ptr<ComputeService> ReplicaManager::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node) return service;
    return nullptr;
}

void ReplicaManager::Log(State& state, uint64_t generation, const std::string& event,
                          uint64_t bytes, uint64_t transfer)
{
    m_events.push_back({state.summary.taskId, generation, state.summary.attempts.at(generation).node,
                       Now(), event, bytes, transfer});
}

void ReplicaManager::Request(uint64_t id)
{
    if (m_states.contains(id)) return;
    const auto& tasks = m_tasks->GetTaskRuntimes();
    const auto task = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t.definition.taskId == id; });
    Require(task != tasks.end() && m_tasks->BeginParallelExecution(id), "replica requires first primary dispatch");
    auto owned = std::make_unique<State>(*task);
    auto& state = *owned;
    auto& r = state.summary;
    r.taskId = id;
    r.totalWork = task->definition.computeWorkUnits;
    r.inputBytes = task->definition.inputBytes;
    r.resultBytes = task->definition.outputBytes;
    r.deadlineNs = task->computeDeadlineTimeNs;
    r.requested = true;
    r.requestedNs = Now();
    auto& primary = r.attempts[0];
    primary.node = task->definition.computeNodeId;
    primary.stage = ReplicaStage::RUNNING;
    primary.computeStartedNs = task->computeStartTimeNs;
    primary.rate = Service(primary.node)->GetComputeRateWorkUnitsPerSecond();
    primary.resultTransfer = task->definition.resultTransferId;
    primary.inputTransfer = task->definition.inputTransferId;
    primary.inputReceivedNs = task->inputTransferCompleteTimeNs;
    primary.inputStartedNs = task->definition.arrivalTimeNs;
    primary.inputMode = "NETWORK";
    r.attempts[1].generation = 1;
    m_states.emplace(id, std::move(owned));
    m_flows.emplace(primary.resultTransfer, Flow{id, 0, false});
    m_network->SetBusinessResult(primary.resultTransfer, false);
    m_network->SetTerminalObserver(primary.resultTransfer, MakeCallback(&ReplicaManager::TransferTerminal, this));
    Log(state, 0, "PRIMARY_COMPUTE_STARTED");
    Log(state, 1, "REPLICA_REQUESTED");
    ProtectionContext context;
    context.attempt = {id, 0};
    context.primaryNode = primary.node;
    context.nowNs = Now();
    context.firstComputeStart = true;
    for (auto service : m_tasks->GetComputeServices())
    {
        const auto node = service->GetNodeId();
        const bool resultReachable = m_tasks->IsSatelliteAvailable(task->definition.resultNodeId) &&
            (node == task->definition.resultNodeId ||
             !m_topology.GetEcmpRouteCandidates(node, task->definition.resultNodeId).empty());
        context.candidates.push_back({node, m_tasks->IsSatelliteAvailable(node) && service->IsComputeAvailable(),
                                      service->IsIdle(), resultReachable});
    }
    context.backupNodeFeasible = [&](uint32_t node) {
        if (!m_tasks->IsSatelliteAvailable(task->definition.sourceNodeId)) return false;
        const auto input = m_network->EstimateAdmissiblePath(task->definition.sourceNodeId, node).TransferTimeNs(r.inputBytes);
        const auto work = ComputeService::CalculateServiceTimeNs(r.totalWork, Service(node)->GetComputeRateWorkUnitsPerSecond());
        const auto budget = r.deadlineNs - Now();
        return input && work <= budget && *input <= budget - work;
    };
    const auto action = m_policy.OnTaskComputeStart(context);
    if (action.kind == ActionKind::NONE)
    {
        r.admissionReason = "NO_FEASIBLE_REPLICA_NODE";
        Log(state, 1, "REPLICA_NOT_ADMITTED");
        return;
    }
    Require(action.kind == ActionKind::START_REPLICA && action.replicaNode, "invalid replica policy action");
    auto& replica = r.attempts[1];
    replica.node = *action.replicaNode;
    auto service = Service(replica.node);
    if (!service->ReserveRecovery(id, 1))
    {
        r.admissionReason = "REPLICA_RESOURCE_UNAVAILABLE";
        Log(state, 1, "REPLICA_NOT_ADMITTED");
        return;
    }
    r.admitted = true;
    r.admissionReason = "ADMITTED";
    replica.stage = ReplicaStage::INPUT;
    replica.rate = service->GetComputeRateWorkUnitsPerSecond();
    replica.reservedNs = Now();
    replica.plannedInputWaitNs = *m_network->EstimateAdmissiblePath(task->definition.sourceNodeId,
        replica.node).TransferTimeNs(r.inputBytes);
    Log(state, 1, "REPLICA_ADMITTED");
    DeliverReplica(state, true);
}

void ReplicaManager::UpdateActual(State& state, ReplicaAttempt& a)
{
    if (a.generation)
    {
        const auto service = Service(a.node);
        if (service && state.summary.admitted)
        {
            const auto work = service->GetRecoveryAccounting(state.summary.taskId, 1);
            a.actualWork = work.executedWork;
            a.actualServiceNs = work.serviceNs;
        }
    }
    else if (a.stage == ReplicaStage::RUNNING)
    {
        const auto snapshot = Service(a.node)->GetRunningTaskSnapshot();
        Require(snapshot && snapshot->taskId == state.summary.taskId, "primary lost actual service");
        a.actualServiceNs = snapshot->elapsedTimeNs;
        a.actualWork = static_cast<uint64_t>(std::min<unsigned __int128>(state.summary.totalWork,
            static_cast<unsigned __int128>(a.actualServiceNs) * a.rate / 1000000000));
    }
    if (a.reservedNs >= 0)
        a.reservedIdleNs = (a.computeStartedNs >= 0 ? a.computeStartedNs :
                           a.terminalNs >= 0 ? a.terminalNs : Now()) - a.reservedNs;
}

void ReplicaManager::PrimaryComputed(uint64_t id, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || state.summary.attempts[0].stage != ReplicaStage::RUNNING) return;
    Require(state.summary.attempts[0].node == node, "wrong primary completion node");
    auto& a = state.summary.attempts[0];
    a.actualWork = state.summary.totalWork;
    a.actualServiceNs = at - a.computeStartedNs;
    Computed(state, 0, at);
}

void ReplicaManager::ReplicaStarted(uint64_t id, uint64_t generation, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    auto& a = state.summary.attempts[1];
    Require(state.live && generation == 1 && a.node == node && a.stage == ReplicaStage::READY,
            "invalid real replica dispatch");
    a.stage = ReplicaStage::RUNNING;
    a.computeStartedNs = at;
    a.reservedIdleNs = at - a.reservedNs;
    Log(state, 1, "REPLICA_COMPUTE_STARTED");
}

void ReplicaManager::ReplicaComputed(uint64_t id, uint64_t generation, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || state.summary.attempts[1].stage != ReplicaStage::RUNNING) return;
    Require(generation == 1 && state.summary.attempts[1].node == node, "wrong replica completion identity");
    UpdateActual(state, state.summary.attempts[1]);
    Computed(state, 1, at);
}

void ReplicaManager::Computed(State& state, uint64_t generation, int64_t at)
{
    auto& a = state.summary.attempts.at(generation);
    if (at > state.summary.deadlineNs)
    {
        Cancel(state, generation, "COMPUTE_DEADLINE_EXCEEDED", true);
        return Deadline(state.summary.taskId);
    }
    Require(m_tasks->ParallelComputed(state.summary.taskId, a.node, at), "logical compute evidence rejected");
    a.computeCompleteNs = at;
    a.stage = ReplicaStage::RESULT;
    a.resultStartedNs = at;
    Log(state, generation, "ATTEMPT_COMPUTE_COMPLETE");
    if (generation)
        DeliverReplica(state, false);
    else
    {
        a.resultMode = "NETWORK";
        Log(state, 0, "PRIMARY_RESULT_STARTED", state.summary.resultBytes, a.resultTransfer);
        m_network->StartTransferNow(a.resultTransfer);
    }
}

void ReplicaManager::DeliverReplica(State& state, bool input)
{
    auto& a = state.summary.attempts[1];
    const auto source = input ? state.task.definition.sourceNodeId : a.node;
    const auto destination = input ? a.node : state.task.definition.resultNodeId;
    const auto bytes = input ? state.summary.inputBytes : state.summary.resultBytes;
    const auto kind = input ? ProtectionTransferKind::REPLICA_INPUT : ProtectionTransferKind::REPLICA_RESULT;
    const std::string prefix = input ? "REPLICA_INPUT" : "REPLICA_RESULT";
    if (input) a.inputStartedNs = Now();
    auto& mode = input ? a.inputMode : a.resultMode;
    mode = source == destination ? "LOCAL" : "NETWORK";
    Log(state, 1, prefix + "_STARTED", bytes);
    if (source == destination)
    {
        state.timers.push_back(LocalDelivery::Schedule(source, destination, bytes,
            [this, &state, input, source] {
                const auto stage = state.summary.attempts[1].stage;
                return state.live && stage == (input ? ReplicaStage::INPUT : ReplicaStage::RESULT) &&
                       m_tasks->IsSatelliteAvailable(source);
            },
            [this, &state, input](uint64_t delivered, int64_t) { Received(state, 1, input, delivered); }));
        return;
    }
    m_ledger.QueueRecovery({state.summary.taskId, 1, kind, 0}, source, destination, bytes, 0, 0,
        [&state, input] { return state.live && state.summary.attempts[1].stage ==
            (input ? ReplicaStage::INPUT : ReplicaStage::RESULT); },
        [this, &state, input, prefix](uint64_t id) {
            if (!id)
            {
                Cancel(state, 1, "REPLICA_TRANSFER_REGISTRATION_FAILED", true);
                if (!Valid(state.summary.attempts[0]) && !m_inFaultBatch)
                    Fail(state, "NO_SURVIVING_ATTEMPT", TaskFailureReason::INPUT_TRANSFER_FAILED);
                return;
            }
            auto& a = state.summary.attempts[1];
            (input ? a.inputTransfer : a.resultTransfer) = id;
            m_flows.emplace(id, Flow{state.summary.taskId, 1, input});
            m_network->SetTerminalObserver(id, MakeCallback(&ReplicaManager::TransferTerminal, this));
            Log(state, 1, prefix + "_REGISTERED", 0, id);
        });
}

void ReplicaManager::StartReplica(State& state)
{
    auto& a = state.summary.attempts[1];
    if (!state.live || a.stage != ReplicaStage::READY || m_inFaultBatch) return;
    if (Now() >= state.summary.deadlineNs) return Deadline(state.summary.taskId);
    if (!Service(a.node)->IsComputeAvailable() && !a.immune) return;
    Require(Service(a.node)->StartReplica(state.summary.taskId, 1, state.summary.totalWork,
        MakeCallback(&ReplicaManager::ReplicaStarted, this), MakeCallback(&ReplicaManager::ReplicaComputed, this)),
        "admitted replica lost its compute reservation");
}

void ReplicaManager::TransferTerminal(uint64_t id, int64_t)
{
    const auto flow = m_flows.at(id);
    auto& state = *m_states.at(flow.taskId);
    auto& a = state.summary.attempts.at(flow.generation);
    if (!state.live || !Valid(a)) return;
    if (m_network->IsCompleted(id))
        return Received(state, flow.generation, flow.input, m_network->GetReceivedBytes(id));
    Cancel(state, flow.generation, "ATTEMPT_TRANSFER_FAILED", true);
    if (!m_inFaultBatch && !Valid(state.summary.attempts[0]) && !Valid(state.summary.attempts[1]))
        Fail(state, "NO_SURVIVING_ATTEMPT", TaskFailureReason::RESULT_TRANSFER_FAILED);
}

void ReplicaManager::Received(State& state, uint64_t generation, bool input, uint64_t bytes)
{
    auto& a = state.summary.attempts.at(generation);
    if (!state.live || !Valid(a)) return;
    Require(bytes == (input ? state.summary.inputBytes : state.summary.resultBytes), "partial replica logical delivery");
    if (input)
    {
        Require(generation == 1 && a.stage == ReplicaStage::INPUT, "duplicate replica INPUT");
        a.inputReceivedNs = Now();
        a.stage = ReplicaStage::READY;
        Log(state, 1, "REPLICA_INPUT_RECEIVED", bytes, a.inputTransfer);
        StartReplica(state);
    }
    else
    {
        Require(a.stage == ReplicaStage::RESULT, "RESULT before attempt compute");
        a.resultCompleteNs = Now();
        Log(state, generation, "ATTEMPT_RESULT_RECEIVED", bytes, a.resultTransfer);
        Win(state, generation);
    }
}

uint64_t ReplicaManager::ServiceNs(const State& state) const
{
    return state.summary.attempts[0].actualServiceNs + state.summary.attempts[1].actualServiceNs;
}

void ReplicaManager::Win(State& state, uint64_t generation)
{
    auto& a = state.summary.attempts.at(generation);
    Require(a.computeCompleteNs >= 0 && a.computeCompleteNs <= state.summary.deadlineNs,
            "winning RESULT missed compute deadline");
    a.stage = ReplicaStage::COMPLETED;
    a.terminalNs = Now();
    Cancel(state, 1 - generation, "OTHER_ATTEMPT_WON", false);
    state.live = false;
    state.summary.winner = generation ? "replica" : "primary";
    state.summary.terminalState = "COMPLETED";
    state.summary.terminalNs = Now();
    state.summary.reason = "FIRST_VALID_RESULT";
    for (auto event : state.timers) Simulator::Cancel(event);
    if (a.resultTransfer) m_network->SetBusinessResult(a.resultTransfer, true);
    Require(m_tasks->ParallelResult(state.summary.taskId, generation, a.node, a.computeCompleteNs,
        a.resultStartedNs, ServiceNs(state), a.resultTransfer, a.resultMode == "LOCAL"),
        "logical parallel winner rejected");
    Log(state, generation, "LOGICAL_RESULT_WINNER", state.summary.resultBytes, a.resultTransfer);
}

void ReplicaManager::Cancel(State& state, uint64_t generation, const std::string& reason, bool failed)
{
    auto& a = state.summary.attempts.at(generation);
    if (!Valid(a)) return;
    UpdateActual(state, a);
    const bool running = a.stage == ReplicaStage::RUNNING;
    a.stage = failed ? ReplicaStage::FAILED : ReplicaStage::CANCELLED;
    a.reason = reason;
    a.terminalNs = Now();
    if (generation)
        Service(a.node)->CancelRecovery(state.summary.taskId, 1);
    else if (running)
        Service(a.node)->CancelRunningTaskForFailure(state.summary.taskId);
    std::vector<uint64_t> ids;
    if (generation && a.inputTransfer) ids.push_back(a.inputTransfer);
    if (a.resultTransfer) ids.push_back(a.resultTransfer);
    m_network->FinalizeTransfersIfActive(ids, TransferTerminalState::CANCELLED,
        TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
    Log(state, generation, failed ? "ATTEMPT_FAILED" : "ATTEMPT_CANCELLED");
}

void ReplicaManager::Fail(State& state, const std::string& reason, TaskFailureReason failure)
{
    if (!state.live) return;
    Cancel(state, 0, reason, true);
    Cancel(state, 1, reason, true);
    state.live = false;
    state.summary.terminalState = "FAILED";
    state.summary.reason = reason;
    state.summary.terminalNs = Now();
    for (auto event : state.timers) Simulator::Cancel(event);
    Require(m_tasks->FailParallel(state.summary.taskId, reason, failure, ServiceNs(state)),
            "logical parallel failure rejected");
    Log(state, 0, "LOGICAL_TASK_FAILED");
}

void ReplicaManager::Deadline(uint64_t id)
{
    auto& state = *m_states.at(id);
    if (!state.live) return;
    for (auto& a : state.summary.attempts)
        if (a.stage == ReplicaStage::RUNNING)
            Service(a.node)->CompleteTaskIfDue(id);
    if (!state.live) return;
    for (auto& a : state.summary.attempts)
        if (Valid(a) && a.stage != ReplicaStage::RESULT)
            Cancel(state, a.generation, "COMPUTE_DEADLINE_EXCEEDED", true);
    if (!Valid(state.summary.attempts[0]) && !Valid(state.summary.attempts[1]))
        Fail(state, "COMPUTE_DEADLINE_EXCEEDED", TaskFailureReason::COMPUTE_DEADLINE_EXCEEDED);
}

std::map<uint32_t, TaskFaultImpact>
ReplicaManager::FaultBatch(const std::vector<TaskFaultNodeChange>& changes)
{
    m_inFaultBatch = true;
    std::map<uint32_t, TaskFaultImpact> impacts;
    std::set<std::pair<uint32_t, uint64_t>> affectedTasks;
    for (auto& [id, owned] : m_states)
    {
        auto& state = *owned;
        if (!state.live) continue;
        for (const auto& change : changes)
            for (auto& a : state.summary.attempts)
            {
                if (!Valid(a)) continue;
                const bool permanent = change.kind == TaskFaultKind::SATELLITE;
                const bool failed = permanent
                    ? (a.node == change.nodeId || state.task.definition.resultNodeId == change.nodeId ||
                       (a.generation && a.stage == ReplicaStage::INPUT && state.task.definition.sourceNodeId == change.nodeId))
                    : (a.node == change.nodeId && a.stage == ReplicaStage::RUNNING && !a.immune);
                if (!failed)
                {
                    if (!permanent && a.node == change.nodeId && a.stage == ReplicaStage::RUNNING && a.immune)
                        Log(state, a.generation, "TAKEOVER_COMPUTE_FAULT_IGNORED");
                    continue;
                }
                UpdateActual(state, a);
                m_tasks->RecordParallelFaultImpact(id, change,
                    a.generation ? "REPLICA_INTERRUPTED" : "PRIMARY_INTERRUPTED",
                    a.computeStartedNs, a.stage == ReplicaStage::RUNNING ? std::optional{a.actualWork} : std::nullopt);
                a.faulted = true;
                if (affectedTasks.emplace(change.nodeId, id).second)
                    ++impacts[change.nodeId].affectedTaskCount;
                std::vector<uint64_t> activeTransfers;
                if (a.generation && a.inputTransfer && !m_network->IsTerminal(a.inputTransfer))
                    activeTransfers.push_back(a.inputTransfer);
                if (a.resultTransfer && !m_network->IsTerminal(a.resultTransfer))
                    activeTransfers.push_back(a.resultTransfer);
                Cancel(state, a.generation, permanent ? "SATELLITE_FAULT_START" : "COMPUTE_FAULT_START", true);
                for (auto transfer : activeTransfers)
                    if (m_network->IsTerminal(transfer))
                        ++impacts[change.nodeId].affectedTransferCount;
            }
    }
    return impacts;
}

void ReplicaManager::BatchComplete()
{
    m_inFaultBatch = false;
    for (auto& [id, owned] : m_states)
    {
        auto& state = *owned;
        if (!state.live) continue;
        auto& primary = state.summary.attempts[0];
        auto& replica = state.summary.attempts[1];
        if (!Valid(primary) && !Valid(replica))
        {
            Fail(state, "NO_SURVIVING_ATTEMPT", TaskFailureReason::COMPUTE_NODE_FAILURE);
            continue;
        }
        if (!Valid(primary) && Valid(replica) && !replica.immune)
        {
            const bool ready = replica.stage == ReplicaStage::RESULT || Service(replica.node)->PromoteReplica(id, 1);
            if (ready)
            {
                replica.immune = true;
                replica.takeoverNs = Now();
                Log(state, 1, "REPLICA_TAKEOVER_AFTER_FAULT_BATCH");
            }
        }
        StartReplica(state);
    }
}

void ReplicaManager::Finalize()
{
    for (auto& [id, state] : m_states)
        if (state->live) Fail(*state, "SIMULATION_ENDED", TaskFailureReason::SIMULATION_ENDED);
    m_tasks->FinalizeSimulation();
    m_ledger.Finalize();
}

std::vector<ReplicaSummary> ReplicaManager::Summaries() const
{
    std::vector<ReplicaSummary> result;
    for (const auto& [id, state] : m_states) result.push_back(state->summary);
    return result;
}
} // namespace ns3::protection
