/* SPDX-License-Identifier: GPL-2.0-only */
#include "frequency-protection-controller.h"

#include "ns3/ipv4.h"
#include "ns3/point-to-point-channel.h"
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
    std::unique_ptr<PlacementPolicy> placement)
    : m_tasks(tasks),
      m_topology(topology),
      m_faults(faults),
      m_manager(tasks, topology, capacity, stopNs),
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
    m_recovery = std::make_unique<RecoveryController>(tasks, topology, m_manager, stopNs, *this);
    m_recovery->SetLoadObserver([this](auto task, auto node, bool active) {
        m_loads.Recovery(task, node, active, Simulator::Now().GetNanoSeconds());
    });
}

FrequencyProtectionController::~FrequencyProtectionController()
{
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
        m_states.try_emplace(event.taskId);
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
        ClosePause(event.taskId, state, event.simulationTimeNs);
        const auto phase = recovery ? ProtectionPhase::RECOVERING : ProtectionPhase::DONE;
        if (state.pending)
            state.stopped = phase;
        else
            state.gate.Stop(phase);
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

std::optional<FrequencyProtectionController::Path> FrequencyProtectionController::EstimatePath(
    uint32_t source,
    uint32_t destination) const
{
    if (!m_tasks->IsSatelliteAvailable(source) || !m_tasks->IsSatelliteAvailable(destination))
        return std::nullopt;
    // Finite representation of zero serialization for the solver's positive-bandwidth domain.
    if (source == destination)
        return Path{std::numeric_limits<double>::max(), 0, true};
    Path path{std::numeric_limits<double>::max(), 0, false};
    std::set<uint32_t> visited;
    while (source != destination)
    {
        if (!visited.insert(source).second)
            return std::nullopt;
        auto routes = m_topology.GetEcmpRouteCandidates(source, destination);
        if (routes.empty())
            return std::nullopt;
        std::sort(routes.begin(), routes.end(), [](const auto& a, const auto& b) {
            return a.outputInterface < b.outputInterface;
        });
        const auto& route = routes.front();
        const auto rate = m_tasks->GetTransferEngine()->GetResidualRateBps(source, route);
        auto ipv4 = m_topology.GetNodeBySatelliteId(source)->GetObject<Ipv4>();
        auto channel = DynamicCast<PointToPointChannel>(
            ipv4->GetNetDevice(route.outputInterface)->GetChannel());
        if (!rate || !channel)
            return std::nullopt;
        path.bytesPerSecond = std::min(path.bytesPerSecond, rate / 8.0);
        TimeValue delay;
        channel->GetAttribute("Delay", delay);
        path.propagationSeconds += delay.Get().GetSeconds();
        source = m_topology.GetNextHopSatelliteId(source, route.outputInterface);
    }
    return path;
}

bool FrequencyProtectionController::BuildResources(FrequencyDecisionRecord& row,
                                                   const TaskRuntime& task,
                                                   State& state)
{
    auto& input = row.input;
    row.pair = state.pair ? state.pair
                          : m_placement->Select({task.definition.computeNodeId,
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
    const auto replay = EstimatePath(task.definition.sourceNodeId, pair.remoteNode);
    const auto base = EstimatePath(task.definition.computeNodeId, pair.remoteNode);
    const auto l1 = EstimatePath(task.definition.computeNodeId, pair.localNode);
    const auto tail = EstimatePath(pair.localNode, pair.remoteNode);
    input.pathAvailable = replay && base && l1 && tail;
    if (!input.pathAvailable)
    {
        if (row.resourceReason.empty()) row.resourceReason = "PATH_UNAVAILABLE";
        return false;
    }
    input.inputBandwidth = replay->bytesPerSecond;
    input.backupBandwidth = std::min(l1->bytesPerSecond, tail->bytesPerSecond);
    TaskStateAdapter layout(task.definition);
    const auto initial = layout.Floor(row.progressWork);
    input.baseTransferSeconds = base->Seconds(task.definition.inputBytes);
    input.stateTransferSeconds =
        base->Seconds(initial ? layout.StateBytes(initial) + layout.HeaderBytes() : 0);
    input.storageDemand = MakeFrequencyStorageEstimator(task.definition,
                                                        row.progressWork,
                                                        m_manager.Inventory(row.taskId));
    return true;
}

void FrequencyProtectionController::BeforeEpoch(const FaultEpochInput& epoch)
{
    const auto found = m_states.find(epoch.taskId);
    if (found == m_states.end())
        return;
    auto& state = found->second;
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
    if (BuildResources(row, task, state))
        row.proposal = m_policy.Evaluate(in);
    else
    {
        row.proposal.phase = phase;
        row.proposal.epochNs = in.risk.epochNs;
        row.proposal.action =
            phase == ProtectionPhase::ON ? FrequencyAction::PAUSE : FrequencyAction::NONE;
        row.proposal.reason = "PLACEMENT_UNAVAILABLE";
    }
    in.storageDemand = {};
    state.gate.Propose(row.proposal);
    state.pending = m_decisions.size();
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
    m_recovery->Finalize();
    m_tasks->FinalizeSimulation();
    m_manager.Finalize();
    for (auto& [id, state] : m_states) ClosePause(id, state, Simulator::Now().GetNanoSeconds());
    if (!m_loads.Empty()) throw std::logic_error("frequency placement ownership leaked");
}

} // namespace ns3::protection
