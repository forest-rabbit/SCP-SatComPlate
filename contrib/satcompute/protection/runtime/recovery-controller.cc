/* SPDX-License-Identifier: GPL-2.0-only */
#include "recovery-controller.h"
#include "../policy/baseline/fa-first-feasible-placement/fa-first-feasible-placement-policy.h"

#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
int64_t
Now()
{
    return Simulator::Now().GetNanoSeconds();
}

int64_t
Duration(uint64_t work, uint64_t rate)
{
    return work ? ComputeService::CalculateServiceTimeNs(work, rate) : 0;
}

void
Require(bool value, const char* message)
{
    if (!value)
        throw std::logic_error(message);
}

const char*
Prefix(ProtectionTransferKind kind)
{
    return kind == ProtectionTransferKind::RECOVERY_INPUT   ? "RECOVERY_INPUT"
           : kind == ProtectionTransferKind::RECOVERY_TAIL  ? "RECOVERY_TAIL"
           : kind == ProtectionTransferKind::RECOVERY_STATE ? "RECOVERY_STATE"
                                                            : "RECOVERY_RESULT";
}
} // namespace

RecoveryController::State::State(const TaskRuntime& runtime,
                                 RecoverySnapshot snapshot,
                                 FaultDefinition fault)
    : task(runtime), attempt(runtime.definition.taskId,
                             runtime.definition.computeNodeId,
                             runtime.computeDeadlineTimeNs),
      layout(runtime.definition)
{
    summary.snapshot = std::move(snapshot);
    summary.fault = std::move(fault);
    summary.primaryNode = runtime.definition.computeNodeId;
    summary.resultBytes = runtime.definition.outputBytes;
}

RecoveryController::RecoveryController(Ptr<TaskCoordinator> tasks,
                                       SatelliteRuntimeView& topology,
                                       CheckpointManager& manager,
                                       int64_t stopNs,
                                       ProtectionPolicy& policy,
                                       RemoteBusyRecoveryPolicy busyPolicy,
                                       PlacementPolicy* recomputePlacement)
    : m_tasks(tasks), m_topology(topology), m_manager(manager),
      m_network(tasks->GetTransferEngine()), m_stopNs(stopNs), m_faultRuntime(policy, {this}),
      m_busyPolicy(busyPolicy), m_recomputePlacement(recomputePlacement)
{
    m_manager.EnableRecoveryRetention();
    m_tasks->SetRecoveryHandler(
        [this](const auto& task, const auto& change) { return Fault(task, change); });
    m_tasks->ConnectTaskObserver(MakeCallback(&RecoveryController::OnTask, this));
}

RecoveryController::~RecoveryController()
{
    m_tasks->SetRecoveryHandler({});
    m_tasks->DisconnectTaskObserver(MakeCallback(&RecoveryController::OnTask, this));
    for (auto& [id, state] : m_states)
        for (auto timer : state->timers)
            Simulator::Cancel(timer);
}

Ptr<ComputeService>
RecoveryController::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node)
            return service;
    return nullptr;
}

void
RecoveryController::Log(State& state,
                        const std::string& event,
                        uint64_t bytes,
                        uint64_t transfer,
                        const std::string& mode)
{
    m_events.push_back({state.task.definition.taskId, 1, Now(), event, mode, bytes, transfer});
    m_manager.RecordRecoveryEvent(state.summary.snapshot, event, bytes, transfer);
}

void
RecoveryController::Later(State& state, int64_t delay, std::function<void()> callback)
{
    Require(delay >= 0, "negative recovery delay");
    if (delay >= m_stopNs - Now())
        return;
    state.timers.push_back(
        Simulator::Schedule(NanoSeconds(delay), [&state, callback = std::move(callback)] {
            if (state.live)
                callback();
        }));
}

bool
RecoveryController::Fault(const TaskRuntime& task, const TaskFaultNodeChange& change)
{
    if (task.attemptGeneration)
    {
        auto found = m_states.find(task.definition.taskId);
        if (found == m_states.end() || !found->second->live)
            return false;
        auto& state = *found->second;
        if (change.kind == TaskFaultKind::COMPUTE)
        {
            if (state.summary.recoveryNode == change.nodeId && state.attempt.ImmuneToComputeFault())
                Log(state, "RECOVERY_COMPUTE_FAULT_IGNORED");
            return false;
        }
        if (m_manager.Pools().contains(change.nodeId))
            m_manager.Pool(change.nodeId).ReleaseTask(task.definition.taskId);
        // F3 always affects the current recovery node and RESULT endpoint. Input/tail
        // sources remain dependencies only until their respective receiver completed.
        const bool inputLost = (Deferred() || state.summary.path == "RECOMPUTE") &&
                               state.summary.inputReceivedNs < 0 &&
                               task.definition.sourceNodeId == change.nodeId;
        const bool tailLost =
            (state.summary.path == "TAIL" || state.summary.path == "MIGRATE_TAIL") &&
            state.summary.tailReceivedNs < 0 && state.summary.snapshot.localNode == change.nodeId;
        const bool stateLost = state.summary.path.starts_with("MIGRATE_") &&
                               state.summary.stateReceivedNs < 0 &&
                               state.summary.snapshot.remoteNode == change.nodeId;
        if (state.summary.recoveryNode == change.nodeId ||
            task.definition.resultNodeId == change.nodeId || inputLost || tailLost || stateLost)
        {
            state.summary.reason = "RECOVERY_F3_SATELLITE_FAILURE";
            for (auto id : state.transfers)
            {
                if (m_network->IsTerminal(id))
                    continue;
                const auto& plans = m_network->GetPlans();
                const auto plan = std::find_if(
                    plans.begin(), plans.end(), [id](const auto& p) { return p.transferId == id; });
                if (plan->sourceSatelliteId == change.nodeId ||
                    plan->destinationSatelliteId == change.nodeId)
                {
                    m_network->FinalizeTransferIfActive(
                        id,
                        TransferTerminalState::FAILED,
                        plan->sourceSatelliteId == change.nodeId
                            ? TransferTerminalReason::SOURCE_SATELLITE_FAILED
                            : TransferTerminalReason::DESTINATION_SATELLITE_FAILED);
                    break; // The terminal callback cleans the owning logical attempt once.
                }
            }
            Fail(state, "RECOVERY_F3_SATELLITE_FAILURE");
            return true;
        }
        return false;
    }
    if (task.state != TASK_RUNNING)
        return false;
    if (task.definition.computeNodeId != change.nodeId)
    {
        if (change.kind == TaskFaultKind::SATELLITE)
        {
            auto snapshot = m_manager.FreezeRecoverySnapshot(task.definition.taskId, Now());
            if (snapshot.phase != "OFF" &&
                (snapshot.localNode == change.nodeId || snapshot.remoteNode == change.nodeId))
            {
                m_manager.QuiesceForRecovery(snapshot);
                m_manager.Pool(change.nodeId).ReleaseTask(task.definition.taskId);
            }
        }
        return false;
    }
    auto snapshot = m_manager.FreezeRecoverySnapshot(task.definition.taskId, Now());
    auto owned = std::make_unique<State>(task, snapshot, change.fault);
    auto& state = *owned;
    if (m_recomputePlacement)
    {
        Require(snapshot.phase == "OFF", "full Recompute cannot own checkpoint state");
        state.summary.plannedCatchupRedoWu = snapshot.actualWork;
        state.summary.plannedPostCatchupWu = task.definition.computeWorkUnits - snapshot.actualWork;
        state.summary.plannedTotalRecoveryWu = task.definition.computeWorkUnits;
    }
    state.summary.checkpointStateExists = snapshot.phase == "ON" && snapshot.remoteObject;
    if (state.summary.checkpointStateExists)
        state.summary.checkpointStateBytes =
            state.layout.CommittedStateBytes(snapshot.remoteWork, m_manager.InputPolicy());
    if (snapshot.phase != "OFF")
    {
        const auto remote = Service(snapshot.remoteNode);
        state.summary.remoteBusyAtFault = remote && !remote->IsIdle();
        state.summary.remoteEligibleAtFault = Eligible(snapshot.remoteNode, state);
    }
    Require(m_states.emplace(task.definition.taskId, std::move(owned)).second,
            "second recovery attempt forbidden");
    Log(state, "FAULT_SNAPSHOT", snapshot.tailBytes);
    // Snapshot is already immutable. Quiescence cancels old callbacks but retains its objects.
    m_manager.QuiesceForRecovery(snapshot);
    Require(m_tasks->BeginRecovery(task.definition.taskId), "primary recovery transition rejected");
    // FaultController applies the communication overlay after task faults at the same ns.
    Later(state, 1, [this, &state] { Decide(state); });
    return true;
}

bool
RecoveryController::Reachable(uint32_t source, uint32_t destination) const
{
    return m_tasks->IsSatelliteAvailable(source) && m_tasks->IsSatelliteAvailable(destination) &&
           (source == destination ||
            !m_topology.GetEcmpRouteCandidates(source, destination).empty());
}

bool
RecoveryController::Eligible(uint32_t node, const State& state) const
{
    const auto service = Service(node);
    return node != state.summary.primaryNode && service && m_tasks->IsSatelliteAvailable(node) &&
           m_tasks->IsComputeAvailable(node) && service->IsIdle() &&
           Reachable(node, state.task.definition.resultNodeId);
}

std::optional<int64_t>
RecoveryController::Estimate(uint32_t source, uint32_t destination, uint64_t bytes) const
{
    if (!Reachable(source, destination))
        return std::nullopt;
    return m_network->EstimateAdmissiblePath(source, destination).TransferTimeNs(bytes);
}

void
RecoveryController::Decide(State& state)
{
    auto& r = state.summary;
    const auto& f = r.snapshot;
    if (Now() >= f.deadlineNs)
        return Fail(state, "DEADLINE_BEFORE_RECOVERY_ACCEPTANCE");
    if (!m_tasks->IsSatelliteAvailable(state.task.definition.resultNodeId))
        return Fail(state, "RESULT_SATELLITE_UNAVAILABLE");
    ProtectionContext context;
    context.attempt = {state.task.definition.taskId, 0};
    context.primaryNode = r.primaryNode;
    context.nowNs = Now();
    context.phase = f.phase == "ON"             ? ProtectionPhase::ON
                    : f.phase == "INITIALIZING" ? ProtectionPhase::INITIALIZING
                                                : ProtectionPhase::OFF;
    m_faultRuntime.OnComputeFault(context);
    if (state.live && r.acceptedNs < 0)
        Fail(state, "POLICY_DECLINED_RECOVERY");
}

bool
RecoveryController::Supports(ActionKind kind) const
{
    return kind == ActionKind::RECOMPUTE;
}

bool
RecoveryController::OnComputeFault(const ProtectionContext& context)
{
    auto& state = *m_states.at(context.attempt.taskId);
    auto& r = state.summary;
    const auto& f = r.snapshot;
    const auto base = f.remoteObject ? m_manager.Pool(f.remoteNode).Find(f.remoteObject) : nullptr;
    // Classification observes the same current predicates used below; it never admits a node.
    if (f.phase != "OFF" && !m_tasks->IsSatelliteAvailable(f.remoteNode))
        r.checkpointFallbackReason = "REMOTE_F3";
    else if (f.phase != "ON" || !base || base->reserved)
        r.checkpointFallbackReason = "STATE_MISSING";
    else if (!m_tasks->IsComputeAvailable(f.remoteNode))
        r.checkpointFallbackReason = "REMOTE_UNAVAILABLE";
    else if (!Service(f.remoteNode)->IsIdle())
        r.checkpointFallbackReason = "REMOTE_BUSY";
    else if (!Reachable(f.remoteNode, state.task.definition.resultNodeId))
        r.checkpointFallbackReason = "PATH_UNAVAILABLE";
    else
        r.checkpointFallbackReason = "OTHER";
    if (f.phase == "ON" && base && !base->reserved && Eligible(f.remoteNode, state))
    {
        const auto input = Deferred() ? Estimate(state.task.definition.sourceNodeId, f.remoteNode,
                                                 state.task.definition.inputBytes)
                                      : std::optional<int64_t>{0};
        if (!input)
        {
            r.checkpointFallbackReason = "INPUT_PATH_UNAVAILABLE";
            return false;
        }
        const auto rate = Service(f.remoteNode)->GetComputeRateWorkUnitsPerSecond();
        r.estimatedRedoNs = *input + Duration(f.actualWork - f.remoteWork, rate);
        auto transfer = f.localWork > f.remoteWork && f.tailBytes &&
                                m_manager.Pool(f.remoteNode).Free() >= f.tailBytes
                            ? Estimate(f.localNode, f.remoteNode, f.tailBytes)
                            : std::nullopt;
        if (transfer)
            r.estimatedTailNs =
                std::max(*input, *transfer + f.remoteCostNs) + Duration(f.actualWork - f.localWork, rate);
        const auto choice = ChooseRecoveryPath(
            transfer ? std::optional{r.estimatedTailNs} : std::nullopt, r.estimatedRedoNs);
        r.path = choice == RecoveryPath::TAIL ? "TAIL" : "REMOTE_REDO";
        state.startWork = choice == RecoveryPath::TAIL ? f.localWork : f.remoteWork;
        const bool accepted = AcceptAndExecute(state, f.remoteNode);
        if (accepted) r.checkpointFallbackReason.clear();
        return accepted;
    }
    if (f.phase == "ON" && base && !base->reserved && m_tasks->IsSatelliteAvailable(f.remoteNode) &&
        AllowsCheckpointRelocation(m_busyPolicy, r.checkpointFallbackReason))
        return TryRelocate(state);
    return false;
}

std::vector<uint32_t>
RecoveryController::Candidates(const State& state) const
{
    PlacementContext context{state.summary.primaryNode, {}};
    for (const auto& [node, pool] : m_manager.Pools())
        context.candidates.push_back({node,
                                     m_tasks->IsComputeAvailable(node) && m_tasks->IsSatelliteAvailable(node),
                                     Service(node) && Service(node)->IsIdle(),
                                     Reachable(node, state.task.definition.resultNodeId)});
    auto nodes = BuildFeasibleBackupNodes(context);
    // Recovery targets retain the established stable-ID baseline for BOTH pair policies.
    // N5C can later replace this ranking without duplicating operation feasibility.
    FaFirstFeasiblePlacementPolicy{}.RankBackupNodes(nodes, context);
    return nodes;
}

bool
RecoveryController::TryRelocate(State& state)
{
    auto& r = state.summary;
    const auto& f = r.snapshot;
    r.relocationAttempted = true;
    r.relocationTrigger = r.checkpointFallbackReason;
    r.relocationFailureReason = "NO_ELIGIBLE_RECOVERY_NODE";
    const auto bytes = state.layout.CommittedStateBytes(f.remoteWork, m_manager.InputPolicy());
    const auto old = m_manager.Pool(f.remoteNode).Find(f.remoteObject);
    Require(old && !old->reserved && old->bytes == bytes,
            "relocation requires exact committed state");
    for (const auto candidate : Candidates(state))
    {
        if (candidate == f.remoteNode || !Eligible(candidate, state))
            continue;
        const auto input = Deferred() ? Estimate(state.task.definition.sourceNodeId, candidate,
                                                 state.task.definition.inputBytes)
                                      : std::optional<int64_t>{0};
        if (!input)
        {
            r.relocationFailureReason = "INPUT_PATH_UNAVAILABLE";
            continue;
        }
        const auto& pool = m_manager.Pools().at(candidate);
        if (pool->Free() < bytes)
        {
            r.relocationFailureReason = "DESTINATION_STORAGE_UNAVAILABLE";
            continue;
        }
        const auto route = m_network->EstimateAdmissiblePath(f.remoteNode, candidate);
        const auto transfer = route.TransferTimeNs(bytes);
        if (!transfer)
        {
            r.relocationFailureReason = route.failureReason;
            continue;
        }
        const auto rate = Service(candidate)->GetComputeRateWorkUnitsPerSecond();
        const auto remaining = Duration(state.layout.Work() - f.actualWork, rate);
        const auto redo = std::max(*input, *transfer) + Duration(f.actualWork - f.remoteWork, rate);
        std::optional<int64_t> tail;
        if (f.localWork > f.remoteWork && f.tailBytes && pool->Free() - bytes >= f.tailBytes)
        {
            const auto tailTransfer = Estimate(f.localNode, candidate, f.tailBytes);
            if (tailTransfer)
                tail = std::max(*input, std::max(*transfer, *tailTransfer) + f.remoteCostNs) +
                       Duration(f.actualWork - f.localWork, rate);
        }
        const auto budget = f.deadlineNs - Now() - remaining;
        const bool redoFits = redo <= budget;
        const bool tailFits = tail && *tail <= budget;
        if (!redoFits && !tailFits)
        {
            r.relocationFailureReason = "CHECKPOINT_DEADLINE_INFEASIBLE";
            continue;
        }
        const bool useTail = tailFits && (!redoFits || *tail < redo);
        r.estimatedMigrateRedoNs = redo;
        r.estimatedMigrateTailNs = tail.value_or(-1);
        if (const auto input = Estimate(state.task.definition.sourceNodeId,
                                        candidate,
                                        state.task.definition.inputBytes))
            r.estimatedRecomputeNs = *input + Duration(f.actualWork, rate);
        r.path = useTail ? "MIGRATE_TAIL" : "MIGRATE_REDO";
        state.startWork = useTail ? f.localWork : f.remoteWork;
        if (AcceptAndExecute(state, candidate))
        {
            r.checkpointFallbackReason.clear();
            return true;
        }
    }
    return false;
}

void
RecoveryController::Execute(const ProtectionContext& context, const ProtectionAction& action)
{
    Require(action.kind == ActionKind::RECOMPUTE && context.attempt.generation == 0,
            "recovery mechanism only executes primary recompute fallback");
    auto& state = *m_states.at(context.attempt.taskId);
    state.summary.path = "RECOMPUTE";
    state.startWork = 0;
    if (m_recomputePlacement)
    {
        PlacementContext placement{state.summary.primaryNode, {}};
        for (const auto& service : m_tasks->GetComputeServices())
        {
            const auto node = service->GetNodeId();
            const auto load = m_placementLoads ? m_placementLoads->Get(node) : PlacementNodeLoad{};
            placement.candidates.push_back({node,
                m_tasks->IsComputeAvailable(node) && m_tasks->IsSatelliteAvailable(node),
                service->IsIdle(), Reachable(node, state.task.definition.resultNodeId),
                false, 0, 0, load.activeBackup, load.activeRecovery});
        }
        const auto feasible = [&](uint32_t candidate) {
            const auto input = Estimate(state.task.definition.sourceNodeId, candidate,
                                         state.task.definition.inputBytes);
            const auto work = Duration(state.layout.Work(), Service(candidate)->GetComputeRateWorkUnitsPerSecond());
            const auto budget = state.summary.snapshot.deadlineNs - Now();
            return input && work <= budget && *input <= budget - work;
        };
        const auto node = m_recomputePlacement->SelectBackupNode(placement, feasible);
        m_recomputePlacement->RecordSelection({state.task.definition.taskId, Now(),
            state.summary.primaryNode, {}, node, "REJECTED", "NO_FEASIBLE_RECOMPUTE_NODE_INPUT_OR_DEADLINE"});
        // Minimal policies choose first; validate only that node, never retry a second one.
        const bool selectedFeasible = node && (m_recomputePlacement->Eligibility() != PlacementEligibility::MINIMAL ||
            (Reachable(*node, state.task.definition.resultNodeId) && feasible(*node)));
        if (selectedFeasible && AcceptAndExecute(state, *node))
        {
            m_recomputePlacement->RecordAdmission(state.task.definition.taskId, Now(), "ACCEPTED", "RECOVERY_ACCEPTED");
            return;
        }
        Log(state, "RECOVERY_DECISION");
        return Fail(state, "NO_FEASIBLE_RECOMPUTE_NODE_INPUT_OR_DEADLINE");
    }
    for (const auto candidate : Candidates(state))
    {
        if (Eligible(candidate, state) &&
            Reachable(state.task.definition.sourceNodeId, candidate) &&
            AcceptAndExecute(state, candidate))
            return;
    }
    Log(state, "RECOVERY_DECISION");
    Fail(state, "NO_ELIGIBLE_RECOVERY_NODE_OR_INPUT");
}

bool
RecoveryController::AcceptAndExecute(State& state, uint32_t node)
{
    auto& r = state.summary;
    const auto& f = r.snapshot;
    if (Deferred() && !Estimate(state.task.definition.sourceNodeId, node, state.task.definition.inputBytes))
        return false;
    state.service = Service(node);
    if (!state.service->ReserveRecovery(state.task.definition.taskId, 1))
        return false;
    Require(state.attempt.AcceptRecovery(node, true, Now()), "recovery attempt acceptance failed");
    Log(state, "RECOVERY_DECISION");
    r.recoveryNode = node;
    r.acceptedNs = Now();
    if (m_loadObserver) m_loadObserver(state.task.definition.taskId, node, true);
    if (Deferred() || r.path == "RECOMPUTE")
        if (const auto estimate = Estimate(state.task.definition.sourceNodeId,
                                           node,
                                           state.task.definition.inputBytes))
        {
            r.plannedInputWaitNs = *estimate;
            r.estimatedRecomputeNs =
                *estimate +
                Duration(f.actualWork, state.service->GetComputeRateWorkUnitsPerSecond());
        }
    // For from-zero recompute this is xf*W planned full catch-up, not charged work.
    // Completion/interruption callbacks separately record only actually executed WU.
    r.plannedCatchupRedoWu = f.actualWork - state.startWork;
    r.plannedPostCatchupWu = state.layout.Work() - f.actualWork;
    r.plannedTotalRecoveryWu = state.layout.Work() - state.startWork;
    r.recoveryRate = state.service->GetComputeRateWorkUnitsPerSecond();
    Log(state, "RECOVERY_ACCEPTED");
    if (Deferred())
    {
        if (r.path == "RECOMPUTE" || r.path == "REMOTE_REDO")
            r.stateReadyNs = Now();
        // Independent flows are all requested now. Canonical registration and the
        // shared network decide actual admission/contention; no artificial serialization.
        Deliver(state, ProtectionTransferKind::RECOVERY_INPUT,
                state.task.definition.sourceNodeId, node, state.task.definition.inputBytes);
        if (!state.live)
            return true;
    }
    if (r.path.starts_with("MIGRATE_"))
    {
        auto& pool = m_manager.Pool(node);
        const auto object = pool.TryReserve(state.task.definition.taskId,
                                            StorageKind::REMOTE_STATE,
                                            r.checkpointStateBytes);
        if (!object)
        {
            Fail(state, "RECOVERY_STATE_CAPACITY_UNAVAILABLE");
            return true;
        }
        state.relocatedObject = *object;
        if (r.path == "MIGRATE_TAIL")
        {
            const auto tail = pool.TryReserve(state.task.definition.taskId,
                                              StorageKind::REMOTE_BATCH,
                                              f.tailBytes);
            if (!tail)
            {
                Fail(state, "RECOVERY_TAIL_CAPACITY_UNAVAILABLE");
                return true;
            }
            state.tailObject = *tail;
        }
        r.relocationFailureReason.clear();
        r.relocationBytes = r.checkpointStateBytes;
        Log(state, "CHECKPOINT_RELOCATION_STARTED", r.relocationBytes);
        Deliver(state,
                ProtectionTransferKind::RECOVERY_STATE,
                f.remoteNode,
                node,
                r.relocationBytes,
                state.relocatedObject);
        if (state.live && r.path == "MIGRATE_TAIL")
            Deliver(state,
                    ProtectionTransferKind::RECOVERY_TAIL,
                    f.localNode,
                    node,
                    f.tailBytes,
                    state.tailObject);
    }
    else if (r.path == "TAIL")
    {
        const auto object = m_manager.Pool(node).TryReserve(
            state.task.definition.taskId, StorageKind::REMOTE_BATCH, f.tailBytes);
        if (!object)
        {
            Fail(state, "RECOVERY_TAIL_CAPACITY_UNAVAILABLE");
            return true;
        }
        state.tailObject = *object;
        Deliver(
            state, ProtectionTransferKind::RECOVERY_TAIL, f.localNode, node, f.tailBytes, *object);
    }
    else if (r.path == "RECOMPUTE")
    {
        m_manager.ReleaseRecoveryState(state.task.definition.taskId);
        if (!Deferred()) Deliver(state,
                ProtectionTransferKind::RECOVERY_INPUT,
                state.task.definition.sourceNodeId,
                node,
                state.task.definition.inputBytes);
    }
    else
        StartCompute(state);
    return true;
}

void
RecoveryController::OnTaskTerminal(uint64_t id)
{
    const auto found = m_states.find(id);
    if (found != m_states.end() && IsTerminalTaskState(found->second->task.state))
        Cleanup(*found->second);
}

void
RecoveryController::Deliver(State& state,
                            ProtectionTransferKind kind,
                            uint32_t source,
                            uint32_t destination,
                            uint64_t bytes,
                            uint64_t object)
{
    if (!Reachable(source, destination))
        return Fail(state, "RECOVERY_DELIVERY_UNREACHABLE");
    const std::string mode = !bytes ? "ZERO_BYTES" : source == destination ? "LOCAL" : "NETWORK";
    auto& r = state.summary;
    if (kind == ProtectionTransferKind::RECOVERY_INPUT)
    {
        Require(r.inputStartedNs < 0, "original recovery INPUT requested more than once");
        r.inputStartedNs = Now();
        r.inputMode = mode;
    }
    else if (kind == ProtectionTransferKind::RECOVERY_TAIL)
        r.tailStartedNs = Now();
    else if (kind == ProtectionTransferKind::RECOVERY_STATE)
        r.stateStartedNs = Now();
    else
    {
        r.resultStartedNs = Now();
        r.resultMode = mode;
    }
    Log(state, std::string(Prefix(kind)) + "_STARTED", bytes, 0, mode);
    if (!bytes)
    {
        Later(state, 1, [this, &state, kind] { Received(state, kind, 0, 0); });
        return;
    }
    if (source == destination)
    {
        state.timers.push_back(LocalDelivery::Schedule(
            source,
            destination,
            bytes,
            [this, &state, source] {
                return state.live && state.attempt.Owns({state.task.definition.taskId, 1}) &&
                       m_tasks->IsSatelliteAvailable(source);
            },
            [this, &state, kind](uint64_t delivered, int64_t) {
                Received(state, kind, delivered, 0);
            }));
        return;
    }
    m_manager.QueueRecovery(
        {state.task.definition.taskId, 1, kind, state.startWork},
        source,
        destination,
        bytes,
        state.startWork,
        object,
        [&state] {
            return state.live && state.attempt.Owns({state.task.definition.taskId, 1});
        },
        [this, &state, kind](uint64_t id) {
            if (!id)
                return Fail(state, "RECOVERY_TRANSFER_REGISTRATION_REJECTED");
            state.transfers.push_back(id);
            if (kind == ProtectionTransferKind::RECOVERY_RESULT)
                state.summary.resultTransferId = id;
            m_flows.emplace(id, std::pair{state.task.definition.taskId, kind});
            m_network->SetTerminalObserver(
                id, MakeCallback(&RecoveryController::TransferTerminal, this));
            Log(state, std::string(Prefix(kind)) + "_REGISTERED", 0, id, "NETWORK");
        });
}

void
RecoveryController::TransferTerminal(uint64_t transferId, int64_t)
{
    auto [id, kind] = m_flows.at(transferId);
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, 1}))
        return;
    if (!m_network->IsCompleted(transferId))
        return Fail(state,
                    state.summary.reason.empty() ? "RECOVERY_TRANSFER_FAILED"
                                                 : state.summary.reason);
    Received(state, kind, m_network->GetReceivedBytes(transferId), transferId);
}

void
RecoveryController::Received(State& state,
                             ProtectionTransferKind kind,
                             uint64_t bytes,
                             uint64_t transferId)
{
    if (!state.live || !state.attempt.Owns({state.task.definition.taskId, 1}))
        return;
    auto& r = state.summary;
    const auto expected = kind == ProtectionTransferKind::RECOVERY_INPUT
                              ? state.task.definition.inputBytes
                          : kind == ProtectionTransferKind::RECOVERY_STATE ? r.checkpointStateBytes
                          : kind == ProtectionTransferKind::RECOVERY_TAIL  ? r.snapshot.tailBytes
                                                                           : r.resultBytes;
    Require(bytes == expected, "recovery logical bytes disagree with receiver");
    if (kind == ProtectionTransferKind::RECOVERY_RESULT)
    {
        r.resultCompleteNs = Now();
        r.resultTransferId = transferId;
        Log(state, "RECOVERY_RESULT_COMPLETE", bytes, transferId, r.resultMode);
        Require(state.attempt.CompleteResult({state.task.definition.taskId, 1}),
                "result attempt mismatch");
        Require(m_tasks->RecoveryResult(state.task.definition.taskId, 1, transferId, !transferId),
                "recovery logical result rejected");
    }
    else if (kind == ProtectionTransferKind::RECOVERY_INPUT)
    {
        r.inputReceivedNs = Now();
        Log(state, "RECOVERY_INPUT_RECEIVED", bytes, transferId, r.inputMode);
        StartCompute(state);
    }
    else if (kind == ProtectionTransferKind::RECOVERY_STATE)
    {
        r.stateReceivedNs = Now();
        Require(m_manager.Pool(*r.recoveryNode).CommitReservation(state.relocatedObject),
                "relocated state lost destination reservation");
        Log(state, "RECOVERY_STATE_COMMITTED", bytes, transferId, bytes ? "NETWORK" : "ZERO_BYTES");
        MigrationReady(state);
    }
    else
    {
        r.tailReceivedNs = Now();
        Require(m_manager.Pool(*r.recoveryNode).CommitReservation(state.tailObject),
                "tail reception lost its reservation");
        Log(state, "RECOVERY_TAIL_RECEIVED", bytes, transferId, transferId ? "NETWORK" : "LOCAL");
        if (r.path == "MIGRATE_TAIL")
        {
            MigrationReady(state);
            return;
        }
        Later(state, r.snapshot.remoteCostNs, [this, &state] {
            const auto& f = state.summary.snapshot;
            Require(m_manager.Pool(*state.summary.recoveryNode)
                        .Merge(f.remoteObject,
                               state.tailObject,
                               state.layout.CommittedStateBytes(f.localWork, m_manager.InputPolicy())),
                    "tail merge lost state");
            state.tailObject = 0;
            state.summary.tailCommitNs = Now();
            Log(state, "RECOVERY_TAIL_COMMIT", f.tailBytes);
            StartCompute(state);
        });
    }
}

void
RecoveryController::MigrationReady(State& state)
{
    auto& r = state.summary;
    if (r.stateReceivedNs < 0 || state.mergeScheduled)
        return;
    if (r.path == "MIGRATE_REDO")
    {
        state.mergeScheduled = true;
        StartCompute(state);
        return;
    }
    if (r.tailReceivedNs < 0)
        return;
    state.mergeScheduled = true;
    Later(state, r.snapshot.remoteCostNs, [this, &state] {
        auto& r = state.summary;
        Require(m_manager.Pool(*r.recoveryNode)
                    .Merge(state.relocatedObject,
                           state.tailObject,
                           state.layout.CommittedStateBytes(r.snapshot.localWork, m_manager.InputPolicy())),
                "migration merge lost state");
        state.tailObject = 0;
        r.tailCommitNs = Now();
        Log(state, "RECOVERY_TAIL_COMMIT", r.snapshot.tailBytes);
        StartCompute(state);
    });
}

void
RecoveryController::StartCompute(State& state)
{
    if (!state.live || state.summary.computeStartedNs >= 0)
        return;
    if (Deferred())
    {
        auto& r = state.summary;
        const bool stateReady = r.path == "RECOMPUTE" || r.path == "REMOTE_REDO" ||
            (r.path == "MIGRATE_REDO" && r.stateReceivedNs >= 0) ||
            ((r.path == "TAIL" || r.path == "MIGRATE_TAIL") && r.tailCommitNs >= 0);
        if (!stateReady)
            return;
        if (r.stateReadyNs < 0)
            r.stateReadyNs = Now();
        if (r.inputReceivedNs < 0)
            return;
        Require(Now() >= std::max(r.inputReceivedNs, r.stateReadyNs),
                "recovery compute preceded its dependency join");
    }
    if (!state.attempt.StartRecovery({state.task.definition.taskId, 1}, Now()))
        return Fail(state, "RECOVERY_COMPUTE_DEADLINE");
    // Adopt valid state into active compute memory; it is no longer extra backup storage.
    if (state.summary.path.starts_with("MIGRATE_"))
        Log(state, "CHECKPOINT_RELOCATION_OWNERSHIP_SECURED", state.summary.relocationBytes);
    m_manager.ReleaseRecoveryState(state.task.definition.taskId);
    const auto work = state.layout.Work() - state.startWork;
    if (!work || !state.service->StartRecovery(state.task.definition.taskId,
                                               1,
                                               work,
                                               state.summary.plannedCatchupRedoWu,
                                               MakeCallback(&RecoveryController::Started, this),
                                               MakeCallback(&RecoveryController::Catchup, this),
                                               MakeCallback(&RecoveryController::Computed, this)))
        Fail(state, "RECOVERY_COMPUTE_SERVICE_REJECTED");
}

void
RecoveryController::Started(uint64_t id, uint64_t generation, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, generation}))
        return;
    Require(m_tasks->RecoveryStarted(id, generation, node), "recovery dispatch rejected");
    state.summary.computeStartedNs = at;
    state.summary.reservedIdleNs = at - state.summary.acceptedNs;
    Log(state, "RECOVERY_COMPUTE_STARTED");
}

void
RecoveryController::Catchup(uint64_t id, uint64_t generation, uint32_t, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, generation}))
        return;
    state.summary.catchupNs = at;
    Log(state, "CATCHUP_REACHED");
}

void
RecoveryController::Computed(uint64_t id, uint64_t generation, uint32_t node, int64_t at)
{
    auto& state = *m_states.at(id);
    if (!state.live || !state.attempt.Owns({id, generation}))
        return;
    if (!state.attempt.CompleteCompute({id, generation}, at))
        return Fail(state, "RECOVERY_COMPUTE_DEADLINE");
    state.summary.computeCompleteNs = at;
    if (m_loadObserver) m_loadObserver(id, node, false);
    Log(state, "RECOVERY_COMPUTE_COMPLETE");
    Require(m_tasks->RecoveryComputed(id, generation, at - state.summary.computeStartedNs),
            "recovery compute completion rejected");
    Deliver(state,
            ProtectionTransferKind::RECOVERY_RESULT,
            node,
            state.task.definition.resultNodeId,
            state.task.definition.outputBytes);
}

void
RecoveryController::Cleanup(State& state)
{
    if (!state.live)
        return;
    state.live = false;
    if (state.summary.recoveryNode && m_loadObserver)
        m_loadObserver(state.task.definition.taskId, *state.summary.recoveryNode, false);
    for (auto event : state.timers)
        Simulator::Cancel(event);
    if (state.service)
    {
        if (state.summary.computeStartedNs < 0 && state.summary.acceptedNs >= 0)
            state.summary.reservedIdleNs = Now() - state.summary.acceptedNs;
        state.service->CancelRecovery(state.task.definition.taskId, 1);
    }
    m_network->FinalizeTransfersIfActive(state.transfers,
                                         TransferTerminalState::CANCELLED,
                                         TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
    m_manager.ReleaseRecoveryState(state.task.definition.taskId);
}

void
RecoveryController::Fail(State& state, const std::string& reason)
{
    if (!state.live)
        return;
    state.summary.reason = reason;
    if (state.summary.path.starts_with("MIGRATE_") && state.summary.computeStartedNs < 0)
    {
        state.summary.relocationFailureReason = reason;
        Log(state, "CHECKPOINT_RELOCATION_FAILED", state.summary.relocationBytes);
    }
    state.attempt.Fail();
    Log(state, "RECOVERY_FAILED");
    m_tasks->FailRecovery(state.task.definition.taskId,
                          reason,
                          reason == "SIMULATION_ENDED" ? TaskFailureReason::SIMULATION_ENDED
                                                       : TaskFailureReason::COMPUTE_NODE_FAILURE);
}

void
RecoveryController::OnTask(const TaskEventRecord& event)
{
    if (!IsTerminalTaskState(event.toState))
        return;
    auto found = m_states.find(event.taskId);
    if (found == m_states.end())
        return;
    auto& state = *found->second;
    if (state.summary.reason.empty())
        state.summary.reason = event.cause;
    state.summary.terminalState = TaskStateToString(event.toState);
    state.summary.terminalNs = event.simulationTimeNs;
    if (event.toState == TASK_FAILED && state.attempt.Stage() != AttemptStage::FAILED)
    {
        if (state.summary.path.starts_with("MIGRATE_") && state.summary.computeStartedNs < 0)
        {
            state.summary.relocationFailureReason = state.summary.reason;
            Log(state, "CHECKPOINT_RELOCATION_FAILED", state.summary.relocationBytes);
        }
        state.attempt.Fail();
        Log(state, "RECOVERY_FAILED");
    }
    Cleanup(state);
}

void
RecoveryController::Finalize()
{
    for (auto& [id, state] : m_states)
        if (state->live)
            Fail(*state, "SIMULATION_ENDED");
}

std::vector<RecoverySummary>
RecoveryController::Summaries() const
{
    std::vector<RecoverySummary> rows;
    std::map<uint64_t, uint64_t> normalCosts;
    for (const auto& row : m_manager.Summaries())
        normalCosts[row.taskId] = row.normalCostNs;
    for (const auto& [id, state] : m_states)
    {
        auto row = state->summary;
        row.normalProtectionCostNs = normalCosts[id];
        row.primaryRate = Service(row.primaryNode)->GetComputeRateWorkUnitsPerSecond();
        if (state->service)
        {
            const auto actual = state->service->GetRecoveryAccounting(id, 1);
            row.actualServiceNs = actual.serviceNs;
            row.actualTotalRecoveryWu = actual.executedWork;
            row.actualCatchupRedoWu = std::min(actual.executedWork, row.plannedCatchupRedoWu);
            row.actualPostCatchupWu = actual.executedWork - row.actualCatchupRedoWu;
        }
        rows.push_back(row);
    }
    return rows;
}
} // namespace ns3::protection
