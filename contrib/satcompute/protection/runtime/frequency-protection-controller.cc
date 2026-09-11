/* SPDX-License-Identifier: GPL-2.0-only */
#include "frequency-protection-controller.h"

#include "ns3/simulator.h"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace ns3::protection
{

FrequencyProtectionController::FrequencyProtectionController(Ptr<TaskCoordinator> tasks,
                                                             SatelliteRuntimeView& topology,
                                                             Ptr<FaultModelEngine> faults,
                                                             uint64_t capacity,
    int64_t stopNs,
    std::unique_ptr<PlacementPolicy> placement,
    RemoteBusyRecoveryPolicy busyPolicy,
    InputStagingPolicy inputPolicy)
    : m_tasks(tasks),
      m_topology(topology),
      m_faults(faults),
      m_manager(tasks, topology, capacity, stopNs, inputPolicy),
      m_placement(placement ? std::move(placement) : std::make_unique<FirstFeasiblePlacementPolicy>())
{
    if (!faults)
        throw std::invalid_argument("frequency protection requires online generate epochs");
    for (auto service : tasks->GetComputeServices()) m_loads.RegisterNode(service->GetNodeId());
    m_manager.SetAssignmentObserver([this](auto task, auto node, bool active) {
        m_loads.Assignment(task, node, active, Simulator::Now().GetNanoSeconds());
    });
    tasks->ConnectTaskObserver(MakeCallback(&FrequencyProtectionController::OnTask, this));
    m_manager.SetInitializationObserver([this](uint64_t id) { Initialized(id); });
    faults->SetEpochObservers(
        [this](const auto& epoch) { BeforeEpoch(epoch); },
        [this](auto time, const auto& outcomes) { AfterEpoch(time, outcomes); });
    tasks->GetTransferEngine()->SetCapacityReleaseObserver([this] { CapacityReleased(); });
    m_recovery = std::make_unique<RecoveryController>(tasks, topology, m_manager, stopNs, *this, busyPolicy);
    m_recovery->SetLoadObserver([this](auto task, auto node, bool active) {
        m_loads.Recovery(task, node, active, Simulator::Now().GetNanoSeconds());
    });
}

FrequencyProtectionController::~FrequencyProtectionController()
{
    m_tasks->GetTransferEngine()->SetCapacityReleaseObserver({});
    if (m_capacityDrain.IsPending()) Simulator::Cancel(m_capacityDrain);
    m_faults->SetEpochObservers({}, {});
    m_tasks->DisconnectTaskObserver(MakeCallback(&FrequencyProtectionController::OnTask, this));
}

const TaskRuntime& FrequencyProtectionController::Task(uint64_t id) const
{
    for (const auto& task : m_tasks->GetTaskRuntimes())
        if (task.definition.taskId == id)
            return task;
    throw std::logic_error("frequency task missing");
}

Ptr<ComputeService> FrequencyProtectionController::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node)
            return service;
    return nullptr;
}

void FrequencyProtectionController::OnTask(const TaskEventRecord& event)
{
    if (event.toState == TASK_RUNNING)
    {
        m_states.try_emplace(event.taskId);
        const auto live = Service(event.nodeId)->GetRunningTaskSnapshot();
        if (live)
        {
            const auto input = m_faults->QueryTaskPrediction(event.nodeId, live->remainingTimeNs);
            if (input)
            {
                const auto q = CombineComputeFaultProbabilities(
                    input->f1Model ? input->f1State.stepFailureProbability : 0,
                    input->f2Model ? input->f2State.stepFailureProbability : 0);
                Evaluate({event.nodeId, event.taskId, q, *input}, "TASK_RUNNING");
                AfterEpoch(event.simulationTimeNs, {{event.nodeId, event.taskId, false, false}});
            }
        }
    }
    auto found = m_states.find(event.taskId);
    if (found == m_states.end())
        return;
    auto& state = found->second;
    const bool terminal = IsTerminalTaskState(event.toState);
    const bool recovery = event.toState == TASK_RECOVERING || event.toState == TASK_RUNNING_BACKUP;
    const bool complete = event.toState == TASK_RESULT_TRANSFERRING;
    if (complete && event.fromState == TASK_RUNNING)
        m_manager.OnTaskComputeComplete({event.taskId, 0});
    if (terminal)
        m_manager.OnTaskTerminal(event.taskId);
    if (terminal || recovery || complete)
    {
        CloseCapacityWait(event.taskId, state, event.simulationTimeNs, "TASK_LEFT_PRIMARY_COMPUTE");
        ClosePause(event.taskId, state, event.simulationTimeNs);
        const auto phase = recovery ? ProtectionPhase::RECOVERING : ProtectionPhase::DONE;
        if (state.pending)
            state.stopped = phase;
        else
            state.gate.Stop(phase);
    }
}

void FrequencyProtectionController::CloseCapacityWait(uint64_t id, State& state, int64_t time,
                                                      const std::string& reason)
{
    if (state.capacityWaitStart)
    {
        m_capacityWaits.push_back({id, *state.capacityWaitStart, time, reason});
        state.capacityWaitStart.reset();
    }
    m_waitingCapacity.erase(id);
}

void FrequencyProtectionController::CapacityReleased()
{
    if (!m_finalized && !m_waitingCapacity.empty() && !m_capacityDrain.IsPending())
        m_capacityDrain = Simulator::ScheduleNow(&FrequencyProtectionController::DrainCapacityRetries, this);
}

void FrequencyProtectionController::DrainCapacityRetries()
{
    if (m_finalized) return;
    // Stable IDs, fresh snapshots, no old proposal and no synthetic fault sample.
    const auto waiting = m_waitingCapacity;
    const auto now = Simulator::Now().GetNanoSeconds();
    for (const auto id : waiting)
    {
        auto& state = m_states.at(id);
        const auto& task = Task(id);
        if (task.state != TASK_RUNNING || task.attemptGeneration ||
            state.gate.Phase() != ProtectionPhase::OFF)
        {
            CloseCapacityWait(id, state, now, "NOT_WAITING_OFF");
            continue;
        }
        if (state.lastCapacityDecisionNs == now) continue;
        const auto live = Service(task.definition.computeNodeId)->GetRunningTaskSnapshot();
        if (!live || live->taskId != id) continue;
        const auto prediction = m_faults->QueryTaskPrediction(task.definition.computeNodeId,
                                                             live->remainingTimeNs);
        if (!prediction) continue;
        const auto q = CombineComputeFaultProbabilities(
            prediction->f1Model ? prediction->f1State.stepFailureProbability : 0,
            prediction->f2Model ? prediction->f2State.stepFailureProbability : 0);
        Evaluate({task.definition.computeNodeId, id, q, *prediction}, "CAPACITY_RELEASE");
        AfterEpoch(now, {{task.definition.computeNodeId, id, false, false}});
    }
}

void FrequencyProtectionController::ClosePause(uint64_t task, State& state, int64_t time)
{
    if (state.pauseStart)
    {
        const auto inventory = m_manager.Inventory(task);
        if (inventory && !inventory->active && inventory->stopNs >= *state.pauseStart)
            time = std::min(time, inventory->stopNs);
        m_pauses.push_back({task, *state.pauseStart, time, state.pauseReason});
        state.pauseStart.reset();
        state.pauseReason.clear();
    }
}

void FrequencyProtectionController::Initialized(uint64_t id)
{
    auto& state = m_states.at(id);
    if (state.gate.Phase() == ProtectionPhase::INITIALIZING)
        state.gate.InitializationCommitted();
}

std::vector<BackupCandidate> FrequencyProtectionController::Candidates(uint32_t primary) const
{
    std::vector<BackupCandidate> result;
    for (auto service : m_tasks->GetComputeServices())
    {
        const auto node = service->GetNodeId();
        if (node == primary)
            continue;
        const auto routes = m_topology.GetEcmpRouteCandidates(primary, node);
        const bool oneHop = std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
            return m_topology.GetNextHopSatelliteId(primary, route.outputInterface) == node;
        });
        result.push_back({node,
                          m_tasks->IsComputeAvailable(node) && m_tasks->IsSatelliteAvailable(node),
                          service->IsIdle(),
                          !routes.empty(),
                          oneHop,
                          0,
                          m_manager.Pools().at(node)->Free(),
                          m_loads.Get(node).activeBackup,
                          m_loads.Get(node).activeRecovery});
    }
    return result;
}

double FrequencyProtectionController::Path::Seconds(uint64_t bytes) const
{
    return local || !bytes ? 0 : bytes / bytesPerSecond + propagationSeconds;
}

std::optional<FrequencyProtectionController::Path>
FrequencyProtectionController::EstimatePath(DecisionPathSnapshot& paths, uint32_t source,
                                            uint32_t destination,
                                            std::string* reason) const
{
    if (!m_tasks->IsSatelliteAvailable(source) || !m_tasks->IsSatelliteAvailable(destination))
    {
        if (reason)
            *reason = "NO_ROUTE";
        return std::nullopt;
    }
    const auto& estimate = paths.Get(source, destination);
    if (reason)
        *reason = estimate.failureReason;
    if (!estimate.admissible)
        return std::nullopt;
    return Path{estimate.local ? std::numeric_limits<double>::max()
                               : estimate.path.admittedRateBps / 8.0,
                estimate.propagationNs / 1e9,
                estimate.local};
}

bool FrequencyProtectionController::BuildResources(FrequencyDecisionRecord& row,
                                                   const TaskRuntime& task,
                                                   State& state, DecisionPathSnapshot& paths)
{
    auto& input = row.input;
    input.inputPolicy = m_manager.InputPolicy();
    row.pair = row.pair ? row.pair : state.pair ? state.pair
                          : m_placement->SelectCheckpointPair({task.definition.computeNodeId,
                                                Candidates(task.definition.computeNodeId)});
    if (!row.pair)
    {
        row.resourceReason = "PLACEMENT_UNAVAILABLE";
        return false;
    }
    const auto pair = *row.pair;
    auto local = Service(pair.localNode), remote = Service(pair.remoteNode);
    row.localLoad = m_loads.Get(pair.localNode);
    row.remoteLoad = m_loads.Get(pair.remoteNode);
    input.localFreeBytes = m_manager.Pools().at(pair.localNode)->Free();
    input.remoteFreeBytes = m_manager.Pools().at(pair.remoteNode)->Free();
    input.recoveryRate = remote->GetComputeRateWorkUnitsPerSecond();
    input.nodeAvailable = m_tasks->IsComputeAvailable(pair.localNode) &&
                          m_tasks->IsSatelliteAvailable(pair.localNode) && local->IsIdle() &&
                          m_tasks->IsComputeAvailable(pair.remoteNode) &&
                          m_tasks->IsSatelliteAvailable(pair.remoteNode) && remote->IsIdle();
    if (!m_tasks->IsComputeAvailable(pair.localNode) || !m_tasks->IsSatelliteAvailable(pair.localNode) ||
        !m_tasks->IsComputeAvailable(pair.remoteNode) || !m_tasks->IsSatelliteAvailable(pair.remoteNode))
        row.resourceReason = "PLACEMENT_UNAVAILABLE";
    else if (!local->IsIdle()) row.resourceReason = "LOCAL_BUSY";
    else if (!remote->IsIdle()) row.resourceReason = "REMOTE_BUSY";
    const auto replay =
        EstimatePath(paths, task.definition.sourceNodeId, pair.remoteNode, &row.replayReason);
    std::string baseReason, localReason, tailReason;
    const auto base = EstimatePath(paths, task.definition.computeNodeId, pair.remoteNode, &baseReason);
    const auto l1 = EstimatePath(paths, task.definition.computeNodeId, pair.localNode, &localReason);
    const auto tail = EstimatePath(paths, pair.localNode, pair.remoteNode, &tailReason);
    input.replayAvailable = replay.has_value();
    input.pathAvailable = base && l1 && tail &&
        (input.inputPolicy == InputStagingPolicy::EAGER || replay);
    if (!input.pathAvailable)
    {
        if (row.resourceReason.empty())
            row.resourceReason = !base ? baseReason : !l1 ? localReason : !tail ? tailReason : row.replayReason;
        return false;
    }
    input.inputBandwidth = replay ? replay->bytesPerSecond : 0;
    input.backupBandwidth = std::min(l1->bytesPerSecond, tail->bytesPerSecond);
    TaskStateAdapter layout(task.definition);
    const auto initial = layout.Floor(row.progressWork);
    input.baseTransferSeconds = input.inputPolicy == InputStagingPolicy::DEFERRED
        ? 0 : base->Seconds(task.definition.inputBytes);
    input.stateTransferSeconds =
        base->Seconds(initial ? layout.StateBytes(initial) + layout.HeaderBytes() : 0);
    input.storageDemand = MakeFrequencyStorageEstimator(task.definition,
                                                        row.progressWork,
                                                        m_manager.Inventory(row.taskId), input.inputPolicy);
    return true;
}

void FrequencyProtectionController::EvaluateOffPairs(FrequencyDecisionRecord& row,
                                                      const TaskRuntime& task, State& state,
                                                      DecisionPathSnapshot& paths)
{
    const PlacementContext context{task.definition.computeNodeId,
                                   Candidates(task.definition.computeNodeId)};
    row.pairStats = BuildFeasiblePlacementPairs(context, [&](auto source, auto destination) {
        return paths.Availability(source, destination);
    });
    if (m_manager.InputPolicy() == InputStagingPolicy::DEFERRED)
    {
        // INPUT is an operation-specific fourth path, not a new placement ranking.
        // Filter before selecting a pair so a blocked source path can retry on capacity
        // release or try another pair instead of violating the three-path preview assertion.
        auto& stats = row.pairStats;
        std::erase_if(stats.pairs, [&](const auto& pair) {
            const auto p = paths.Availability(task.definition.sourceNodeId, pair.remoteNode);
            if (!m_tasks->IsSatelliteAvailable(task.definition.sourceNodeId) || !p.reachable)
                ++stats.skipNoRoute;
            else if (p.admissible)
                return false;
            else if (p.reason == "NO_ADMISSIBLE_PATH")
                ++stats.skipNoCapacity;
            else
                ++stats.skipOther;
            return true;
        });
        if (stats.nodeFeasible && stats.pairs.empty())
            stats.reason = stats.skipNoCapacity ? "NO_CAPACITY_NOW"
                         : stats.skipOther ? "PATH_ADMISSION_UNAVAILABLE" : "NO_ROUTE";
    }
    auto pairs = std::move(row.pairStats.pairs);
    row.pairPathFeasible = pairs.size();
    m_placement->RankPairs(pairs, context);
    if (pairs.empty())
    {
        row.resourceReason = row.pairStats.reason;
        row.proposal.phase = ProtectionPhase::OFF;
        row.proposal.epochNs = row.input.risk.epochNs;
        row.proposal.reason = row.resourceReason;
        return;
    }
    // Feasibility before ranking, then first frequency-hard-feasible pair. Never shop by J.
    for (const auto& pair : pairs)
    {
        row.pair = pair;
        row.resourceReason.clear();
        if (!BuildResources(row, task, state, paths))
            throw std::logic_error("read-only pair preview changed within one decision");
        ++row.pairHardChecked;
        row.proposal = m_policy.Evaluate(row.input);
        const auto& reason = row.proposal.reason;
        if (reason == "STORAGE_INFEASIBLE") ++row.pairSkipStorage;
        else if (reason == "DEADLINE_INFEASIBLE" || reason == "INITIALIZATION_TOO_LATE")
            ++row.pairSkipDeadline;
        else
        {
            ++row.pairHardFeasible;
            return;
        }
        row.resourceReason = reason == "INITIALIZATION_TOO_LATE" ? "DEADLINE_INFEASIBLE" : reason;
    }
}

void FrequencyProtectionController::BeforeEpoch(const FaultEpochInput& epoch)
{
    Evaluate(epoch, "FAULT_EPOCH");
}

void
FrequencyProtectionController::Evaluate(const FaultEpochInput& epoch, const std::string& trigger)
{
    const auto found = m_states.find(epoch.taskId);
    if (found == m_states.end())
        return;
    auto& state = found->second;
    const bool retry = trigger == "CAPACITY_RELEASE";
    if ((retry ? state.lastCapacityDecisionNs : state.lastDecisionNs) == epoch.prediction.predictionTimeNs)
        return;
    const auto& task = Task(epoch.taskId);
    if (task.state != TASK_RUNNING || task.attemptGeneration ||
        epoch.nodeId != task.definition.computeNodeId ||
        epoch.prediction.remainingComputeTimeNs <= 0)
        return;
    const auto inventory = m_manager.Inventory(epoch.taskId);
    if (inventory && !inventory->active)
    {
        ClosePause(epoch.taskId, state, epoch.prediction.predictionTimeNs);
        state.gate.Stop(ProtectionPhase::DONE);
    }
    const auto phase = state.gate.Phase();
    if (phase != ProtectionPhase::OFF && phase != ProtectionPhase::ON)
        return;
    FrequencyDecisionRecord row;
    row.trigger = trigger;
    row.waitingBefore = state.capacityWaitStart.has_value();
    row.capacityWaitStartNs = state.capacityWaitStart.value_or(-1);
    if (retry)
    {
        ++state.capacityRetryCount;
        state.lastCapacityDecisionNs = epoch.prediction.predictionTimeNs;
    }
    row.capacityRetryCount = state.capacityRetryCount;
    row.pF1 = epoch.prediction.f1Model ? epoch.prediction.f1State.stepFailureProbability : 0;
    row.pF2 = epoch.prediction.f2Model ? epoch.prediction.f2State.stepFailureProbability : 0;
    row.firstSampleNs =
        epoch.prediction.firstSampleTimeNs.value_or(epoch.prediction.predictionTimeNs);
    row.taskId = epoch.taskId;
    row.profile = task.definition.taskProfile;
    row.previous = state.gate.CurrentConfig();
    auto& in = row.input;
    in.phase = phase;
    in.risk = MakeFrequencyRisk(epoch.currentSampleProbability, epoch.prediction);
    in.primaryRate = Service(epoch.nodeId)->GetComputeRateWorkUnitsPerSecond();
    in.work = task.definition.computeWorkUnits;
    row.progressWork = static_cast<uint64_t>(std::min<unsigned __int128>(
        task.definition.computeWorkUnits,
        static_cast<unsigned __int128>(in.risk.epochNs - task.computeStartTimeNs) *
            Service(epoch.nodeId)->GetComputeRateWorkUnitsPerSecond() / 1000000000));
    in.progress = row.progressWork / in.work;
    in.remainingSeconds = epoch.prediction.remainingComputeTimeNs / 1e9;
    in.deadlineNs = task.computeDeadlineTimeNs;
    in.inputBytes = task.definition.inputBytes;
    in.variableBytes = TaskStateAdapter(task.definition).VariableBytes();
    in.costs = GetProtectionCosts(static_cast<uint64_t>(in.variableBytes));
    DecisionPathSnapshot paths([this](auto source, auto destination) {
        return m_tasks->GetTransferEngine()->EstimateAdmissiblePath(source, destination);
    });
    if (phase == ProtectionPhase::OFF)
        EvaluateOffPairs(row, task, state, paths);
    else if (BuildResources(row, task, state, paths))
        row.proposal = m_policy.Evaluate(in);
    else
    {
        row.proposal.phase = phase;
        row.proposal.epochNs = in.risk.epochNs;
        row.proposal.action =
            phase == ProtectionPhase::ON ? FrequencyAction::PAUSE : FrequencyAction::NONE;
        row.proposal.reason = "PLACEMENT_UNAVAILABLE";
    }
    if (phase == ProtectionPhase::OFF && row.resourceReason == "NO_CAPACITY_NOW" &&
        in.risk.pFailBeforeFinish > 0)
    {
        if (!state.capacityWaitStart) state.capacityWaitStart = in.risk.epochNs;
        m_waitingCapacity.insert(epoch.taskId);
        row.capacityWaitStartNs = *state.capacityWaitStart;
    }
    else if (state.capacityWaitStart)
    {
        row.capacityWaitEndNs = in.risk.epochNs;
        CloseCapacityWait(epoch.taskId, state, in.risk.epochNs, row.proposal.reason);
    }
    row.waitingAfter = state.capacityWaitStart.has_value();
    in.storageDemand = {};
    state.gate.Propose(row.proposal, retry);
    state.pending = m_decisions.size();
    state.lastDecisionNs = in.risk.epochNs;
    m_decisions.push_back(std::move(row));
}

void FrequencyProtectionController::AfterEpoch(int64_t time,
                                               const std::vector<FaultEpochOutcome>& outcomes)
{
    for (const auto& outcome : outcomes)
    {
        auto found = m_states.find(outcome.taskId);
        if (found == m_states.end() || !found->second.pending)
            continue;
        auto& state = found->second;
        auto& row = m_decisions.at(*state.pending);
        const auto& task = Task(outcome.taskId);
        bool running = task.state == TASK_RUNNING && !task.attemptGeneration && !state.stopped;
        // The entire same-time fault batch has applied. A peer can have failed too.
        if (running && row.proposal.action == FrequencyAction::START)
        {
            for (auto node : {row.pair->localNode, row.pair->remoteNode})
                running = running && m_tasks->IsComputeAvailable(node) &&
                          m_tasks->IsSatelliteAvailable(node) && Service(node)->IsIdle();
        }
        row.sampled = outcome.sampled;
        row.faultHit = outcome.faultHit;
        row.committed = state.gate.Resolve(time, outcome.faultHit, running);
        state.pending.reset();
        row.reason = outcome.faultHit ? "CURRENT_FAULT_HIT"
                     : !running       ? "POST_FAULT_UNAVAILABLE"
                                      : row.proposal.reason;
        if (row.committed && row.proposal.action == FrequencyAction::START)
        {
            row.reason = row.trigger == "CAPACITY_RELEASE" ? "START_AFTER_CAPACITY_RELEASE"
                       : row.trigger == "TASK_RUNNING" ? "START_TASK_RUNNING" : "START_FAULT_EPOCH";
            row.capacityRetrySuccess = row.trigger == "CAPACITY_RELEASE";
        }
        row.waitingAfter = state.capacityWaitStart.has_value();
        if (!row.waitingAfter && row.capacityWaitStartNs >= 0 && row.capacityWaitEndNs < 0)
            row.capacityWaitEndNs = time;
        if (row.committed)
        {
            if (row.proposal.action == FrequencyAction::PAUSE)
            {
                const auto reason = row.resourceReason.empty() ? row.proposal.reason : row.resourceReason;
                if (!state.pauseStart || state.pauseReason != reason)
                {
                    ClosePause(row.taskId, state, time);
                    state.pauseStart = time;
                    state.pauseReason = reason;
                }
            }
            else ClosePause(row.taskId, state, time);
            const auto config = state.gate.CurrentConfig();
            if (row.proposal.action == FrequencyAction::START)
            {
                state.pair = row.pair;
                ProtectionContext context;
                context.attempt = {row.taskId, 0};
                context.primaryNode = task.definition.computeNodeId;
                context.nowNs = time;
                m_manager.Execute(context,
                                  {ActionKind::START_CHECKPOINT,
                                   CheckpointConfiguration{config->deltaPermille,
                                                           config->batchN,
                                                           state.pair->localNode,
                                                           state.pair->remoteNode}});
            }
            else if (row.proposal.action == FrequencyAction::UPDATE)
            {
                if (!m_manager.UpdateFutureConfiguration(row.taskId,
                                                         config->deltaPermille,
                                                         config->batchN))
                    throw std::logic_error("surviving frequency update lost live mechanism");
            }
            else if (!m_manager.PauseFutureProtection(row.taskId))
                throw std::logic_error("surviving frequency pause lost live mechanism");
        }
        if (state.stopped)
        {
            state.gate.Stop(*state.stopped);
            state.stopped.reset();
        }
        row.committedConfig = state.gate.CurrentConfig();
        row.phaseAfter = state.gate.Phase();
    }
}

ProtectionAction FrequencyProtectionController::OnComputeFault(const ProtectionContext& context)
{
    if (!context.attempt.taskId || context.attempt.generation ||
        context.phase == ProtectionPhase::DONE || context.phase == ProtectionPhase::RECOVERING)
        return {};
    return {ActionKind::RECOMPUTE, std::nullopt};
}

void FrequencyProtectionController::Finalize()
{
    m_finalized = true;
    m_tasks->GetTransferEngine()->SetCapacityReleaseObserver({});
    if (m_capacityDrain.IsPending()) Simulator::Cancel(m_capacityDrain);
    m_recovery->Finalize();
    m_tasks->FinalizeSimulation();
    m_manager.Finalize();
    for (auto& [id, state] : m_states)
    {
        ClosePause(id, state, Simulator::Now().GetNanoSeconds());
        CloseCapacityWait(id, state, Simulator::Now().GetNanoSeconds(), "FINALIZE");
    }
    if (!m_loads.Empty()) throw std::logic_error("frequency placement ownership leaked");
}

} // namespace ns3::protection
