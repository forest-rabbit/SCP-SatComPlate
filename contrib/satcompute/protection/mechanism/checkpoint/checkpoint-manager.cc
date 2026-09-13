/* SPDX-License-Identifier: GPL-2.0-only */
#include "checkpoint-manager.h"
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

void
Require(bool condition, const char* text)
{
    if (!condition)
        throw std::logic_error(text);
}

uint64_t
MaximumId(Ptr<TaskCoordinator> tasks)
{
    if (!tasks)
        throw std::invalid_argument("checkpoint requires tasks");
    uint64_t maximum = 0;
    for (const auto& plan : tasks->GetTransferEngine()->GetPlans())
        maximum = std::max(maximum, plan.transferId);
    return maximum;
}
} // namespace

CheckpointManager::State::State(const TaskRuntime& runtime,
                                CheckpointConfiguration configuration,
                                uint64_t captured,
                                int64_t now,
                                uint64_t computeRate)
    : task(runtime), config(configuration), layout(runtime.definition),
      progress(layout, captured, now), rate(computeRate), initial(captured), triggered(captured)
{
    auto costs = GetProtectionCosts(layout.VariableBytes());
    summary.taskId = task.definition.taskId;
    summary.inputBytes = task.definition.inputBytes;
    summary.work = layout.Work();
    summary.variableBytes = layout.VariableBytes();
    summary.primaryNode = task.definition.computeNodeId;
    summary.localNode = config.localNode;
    summary.remoteNode = config.remoteNode;
    summary.deltaPermille = config.deltaPermille;
    summary.batchN = config.batchN;
    summary.startNs = now;
    summary.localCostNs = costs.localNs;
    summary.remoteCostNs = costs.remoteNs;
    summary.primaryRate = computeRate;
}

CheckpointManager::CheckpointManager(Ptr<TaskCoordinator> tasks,
                                     SatelliteRuntimeView& topology,
                                     uint64_t capacity,
                                     int64_t stopNs,
                                     InputStagingPolicy inputPolicy)
    : m_tasks(tasks), m_network(tasks ? tasks->GetTransferEngine() : nullptr), m_stopNs(stopNs),
      m_inputPolicy(inputPolicy), m_ids(MaximumId(tasks))
{
    if (stopNs <= 0)
        throw std::invalid_argument("invalid checkpoint duration");
    for (const auto& service : tasks->GetComputeServices())
    {
        Require(topology.HasSatelliteId(service->GetNodeId()), "unknown compute satellite");
        m_pools.emplace(service->GetNodeId(), std::make_unique<BackupStoragePool>(capacity));
    }
    // Reject legacy/untyped or incompatible task budgets before the simulation starts.
    if (StateOnlyInitialization(m_inputPolicy))
        for (const auto& [node, pool] : m_pools)
            pool->SetPeakObserver([this] {
                unsigned __int128 total = 0;
                for (const auto& [id, current] : m_pools)
                    total += current->Used() + current->Reserved();
                Require(total <= std::numeric_limits<uint64_t>::max(), "global storage peak overflow");
                m_globalStoragePeakBytes = std::max(m_globalStoragePeakBytes, static_cast<uint64_t>(total));
            });
    for (const auto& task : tasks->GetTaskRuntimes())
        TaskStateAdapter{task.definition};
}

CheckpointManager::~CheckpointManager()
{
    for (auto& [id, input] : m_inputs) Simulator::Cancel(input.localEvent);
    for (auto& [id, state] : m_states)
        for (auto event : state->timers)
            Simulator::Cancel(event);
    for (auto& [time, event] : m_flushEvents)
        Simulator::Cancel(event);
}

bool
CheckpointManager::Supports(ActionKind kind) const
{
    return kind == ActionKind::START_CHECKPOINT;
}

bool
CheckpointManager::OnComputeFault(const ProtectionContext&)
{
    return false;
}

uint64_t
CheckpointManager::Actual(const State& state) const
{
    const auto elapsed = std::max<int64_t>(0, Now() - state.task.computeStartTimeNs);
    return static_cast<uint64_t>(std::min<unsigned __int128>(
        state.layout.Work(), static_cast<unsigned __int128>(elapsed) * state.rate / 1000000000));
}

bool
CheckpointManager::Live(State& state)
{
    if (!state.active)
        return false;
    const auto duration = ComputeService::CalculateServiceTimeNs(state.layout.Work(), state.rate);
    // Inclusive compute completion beats a protection callback independently of event UID.
    if (Now() >= m_stopNs || state.task.state != TASK_RUNNING ||
        Now() - state.task.computeStartTimeNs >= duration)
    {
        Stop(state, Now() >= m_stopNs ? "SIMULATION_ENDED" : "COMPUTE_ENDED");
        return false;
    }
    return true;
}

EventId
CheckpointManager::Later(State& state, int64_t at, std::function<void()> callback)
{
    Require(at >= Now(), "protection event scheduled in the past");
    if (at >= m_stopNs)
        return {};
    state.timers.push_back(Simulator::Schedule(NanoSeconds(at - Now()),
                                               [this, &state, callback = std::move(callback)] {
                                                   if (Live(state))
                                                       callback();
                                               }));
    return state.timers.back();
}

void
CheckpointManager::Log(State& state,
                       const std::string& event,
                       uint64_t work,
                       uint64_t bytes,
                       uint32_t node,
                       uint64_t object,
                       uint64_t transfer)
{
    if (event == "INIT_STATE_GENERATED")
        ++state.summary.initGenerated;
    if (event == "INIT_COST_COMMITTED")
        ++state.summary.initCommitted;
    if (event == "L1_GENERATED")
        ++state.summary.localGeneratedCostCount;
    if (event == "REMOTE_COST_COMMITTED")
        ++state.summary.remoteCommittedCostCount;
    const auto snapshot = state.progress.Current();
    const auto& local = *m_pools.at(state.config.localNode);
    const auto& remote = *m_pools.at(state.config.remoteNode);
    Require(snapshot.remoteWork <= snapshot.localWork && snapshot.localWork <= Actual(state),
            "checkpoint progress exceeds contiguous received/actual work");
    state.summary.localWork = snapshot.localWork;
    state.summary.remoteWork = snapshot.remoteWork;
    m_events.push_back({state.summary.taskId,
                        0,
                        Now(),
                        event,
                        state.config.localNode,
                        state.config.remoteNode,
                        work,
                        bytes,
                        snapshot.localWork,
                        snapshot.remoteWork,
                        Actual(state),
                        local.Used(),
                        local.Reserved(),
                        remote.Used(),
                        remote.Reserved(),
                        node,
                        object,
                        transfer});
}

std::optional<uint64_t>
CheckpointManager::Reserve(
    State& state, uint32_t node, StorageKind kind, uint64_t bytes, uint64_t work)
{
    auto object = m_pools.at(node)->TryReserve(state.summary.taskId, kind, bytes);
    Log(state,
        object ? "STORAGE_RESERVED" : "STORAGE_RESERVATION_FAILED",
        work,
        bytes,
        node,
        object.value_or(0));
    return object;
}

void
CheckpointManager::Execute(const ProtectionContext& context, const ProtectionAction& action)
{
    if (action.kind != ActionKind::START_CHECKPOINT || !action.checkpoint ||
        context.attempt.generation != 0 || context.nowNs != Now())
        throw std::invalid_argument("G2 supports only current primary checkpoint START");
    if (m_states.contains(context.attempt.taskId))
        return;
    auto task = std::find_if(
        m_tasks->GetTaskRuntimes().begin(),
        m_tasks->GetTaskRuntimes().end(),
        [&](const auto& task) { return task.definition.taskId == context.attempt.taskId; });
    Require(task != m_tasks->GetTaskRuntimes().end(), "checkpoint task not found");
    if (task->state != TASK_RUNNING)
        return;
    const auto config = *action.checkpoint;
    Require(config.deltaPermille && config.deltaPermille <= 1000 && config.batchN &&
                config.batchN <= 1000 / config.deltaPermille &&
                config.localNode != config.remoteNode && config.localNode != context.primaryNode &&
                config.remoteNode != context.primaryNode && m_pools.contains(config.localNode) &&
                m_pools.contains(config.remoteNode),
            "invalid checkpoint placement/configuration");
    uint64_t rate = 0;
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == task->definition.computeNodeId)
            rate = service->GetComputeRateWorkUnitsPerSecond();
    Require(rate != 0, "missing primary compute rate");
    TaskStateAdapter layout(task->definition);
    const auto actual = static_cast<uint64_t>(std::min<unsigned __int128>(
        layout.Work(),
        static_cast<unsigned __int128>(Now() - task->computeStartTimeNs) * rate / 1000000000));
    auto owned = std::make_unique<State>(*task, config, layout.Floor(actual), Now(), rate);
    auto& state = *owned;
    m_states.emplace(context.attempt.taskId, std::move(owned));
    Log(state, "START", state.initial);
    if (!Live(state))
        return;
    const auto base = Reserve(state,
                              config.remoteNode,
                              StorageKind::REMOTE_STATE,
                              StateOnlyInitialization(m_inputPolicy) ? 0 : task->definition.inputBytes,
                              state.initial);
    if (!base)
    {
        Stop(state, "INIT_RESERVATION_FAILED");
        return;
    }
    state.baseObject = *base;
    if (m_assignmentObserver)
        m_assignmentObserver(state.summary.taskId, config.remoteNode, true);
    if (StateOnlyInitialization(m_inputPolicy))
    {
        Require(m_pools.at(config.remoteNode)->CommitReservation(*base),
                "logical state identity reservation missing");
        state.baseReceivedNs = Now();
        Log(state, "INIT_STATE_IDENTITY_READY", state.initial, 0, config.remoteNode, *base);
    }
    else Queue(state,
          ProtectionTransferKind::INIT_BASE,
          0,
          context.primaryNode,
          config.remoteNode,
          task->definition.inputBytes,
          state.initial,
          *base);
    Later(state, Now() + state.summary.localCostNs, [this, &state] {
        const uint64_t bytes = state.initial == 0 ? 0
                                                  : state.layout.StateBytes(state.initial) +
                                                        state.layout.HeaderBytes();
        Log(state, "INIT_STATE_GENERATED", state.initial, bytes);
        auto object =
            Reserve(state, state.config.remoteNode, StorageKind::INIT_TEMP, bytes, state.initial);
        if (!object)
        {
            Stop(state, "INIT_RESERVATION_FAILED");
            return;
        }
        state.initObject = *object;
        if (bytes == 0)
        {
            Require(m_pools.at(state.config.remoteNode)->CommitReservation(*object),
                    "zero init state reservation missing");
            state.stateReceivedNs = Now();
            Log(state,
                "INIT_STATE_LOGICAL_COMPLETE",
                state.initial,
                0,
                state.config.remoteNode,
                *object);
            InitializationReceived(state);
        }
        else
            Queue(state,
                  ProtectionTransferKind::INIT_STATE,
                  0,
                  state.summary.primaryNode,
                  state.config.remoteNode,
                  bytes,
                  state.initial,
                  *object);
    });
}

void
CheckpointManager::Queue(State& state,
                         ProtectionTransferKind kind,
                         uint64_t sequence,
                         uint32_t source,
                         uint32_t destination,
                         uint64_t bytes,
                         uint64_t work,
                         uint64_t object)
{
    Require(bytes != 0, "zero-byte state must use logical completion, not UDP");
    const auto entry = m_pools.at(destination)->Find(object);
    Require(entry && entry->reserved && entry->bytes == bytes &&
                entry->taskId == state.summary.taskId,
            "flow requires exact prior reservation");
    const auto time = Now();
    ProtectionTransferKey key{state.summary.taskId, 0, kind, sequence};
    Require(m_requests[time]
                .emplace(key, Request{key, source, destination, bytes, work, object, time})
                .second,
            "duplicate protection transfer request key");
    if (!m_flushEvents.contains(time))
        m_flushEvents.emplace(
            time, Simulator::Schedule(NanoSeconds(1), &CheckpointManager::Flush, this, time));
}

void
CheckpointManager::Flush(int64_t time)
{
    auto requests = m_requests.extract(time);
    m_flushEvents.erase(time);
    if (requests.empty())
        return;
    // All causal creations at time have run. Sorted key order is invariant to their UIDs.
    for (const auto& [key, request] : requests.mapped())
    {
        if (request.live)
        {
            if (!request.live())
                continue;
            const auto id = m_ids.Next();
            NetworkTransfer plan;
            plan.transferId = id;
            plan.sourceSatelliteId = request.source;
            plan.destinationSatelliteId = request.destination;
            plan.sizeBytes = request.bytes;
            try
            {
                m_network->RegisterRuntimePlan(plan,
                                               key.kind == ProtectionTransferKind::RECOVERY_RESULT);
            }
            catch (const NetworkTransferConfigError&)
            {
                request.registered(0); // Explicit rejected registration, not a synthetic flow.
                continue;
            }
            if (key.kind != ProtectionTransferKind::RECOVERY_RESULT)
                m_flows.push_back({key,
                                   id,
                                   request.bytes,
                                   request.work,
                                   request.object,
                                   request.destination,
                                   time});
            request.registered(id);
            m_network->StartTransferNow(id);
            continue;
        }
        auto& state = *m_states.at(key.taskId);
        if (!Live(state))
            continue;
        uint64_t id;
        try
        {
            id = m_ids.Next();
            NetworkTransfer plan;
            plan.transferId = id;
            plan.sourceSatelliteId = request.source;
            plan.destinationSatelliteId = request.destination;
            plan.sizeBytes = request.bytes;
            m_network->RegisterRuntimePlan(plan);
        }
        catch (const std::runtime_error&)
        {
            Log(state,
                "TRANSFER_REGISTRATION_FAILED",
                request.work,
                request.bytes,
                request.destination,
                request.object);
            Stop(state, "TRANSFER_REGISTRATION_FAILED");
            continue;
        }
        m_flowIndexes.emplace(id, m_flows.size());
        m_flows.push_back(
            {key, id, request.bytes, request.work, request.object, request.destination, time});
        m_network->SetTerminalObserver(id,
                                       MakeCallback(&CheckpointManager::TransferTerminal, this));
        Log(state,
            "TRANSFER_REGISTERED",
            request.work,
            request.bytes,
            request.destination,
            request.object,
            id);
        m_network->StartTransferNow(id);
        Log(state,
            "TRANSFER_STARTED",
            request.work,
            request.bytes,
            request.destination,
            request.object,
            id);
    }
}

void
CheckpointManager::TransferTerminal(uint64_t id, int64_t at)
{
    const auto flow = m_flows.at(m_flowIndexes.at(id));
    auto& state = *m_states.at(flow.key.taskId);
    auto& pool = *m_pools.at(flow.storageNode);
    if (!m_network->IsCompleted(id) || !Live(state))
    {
        pool.ReleaseReservation(flow.storageObject);
        Log(state,
            "TRANSFER_DISCARDED",
            flow.work,
            flow.bytes,
            flow.storageNode,
            flow.storageObject,
            id);
        if (state.active)
        {
            if (flow.key.kind == ProtectionTransferKind::REMOTE_BATCH)
            {
                state.batchObject = 0;
                state.batchInFlight = false;
                state.batchBlocked = true;
            }
            // An unreceived L1 remains an explicit gap; later receipts cannot skip it.
            else if (flow.key.kind != ProtectionTransferKind::L1)
                Stop(state, "INITIALIZATION_TRANSFER_FAILED");
        }
        return;
    }
    Require(m_network->GetReceivedBytes(id) == flow.bytes &&
                pool.CommitReservation(flow.storageObject),
            "real receiver/storage mismatch");
    Log(state,
        "TRANSFER_RECEIVED",
        flow.work,
        flow.bytes,
        flow.storageNode,
        flow.storageObject,
        id);
    switch (flow.key.kind)
    {
    case ProtectionTransferKind::INIT_BASE:
        state.baseReceivedNs = at;
        Log(state, "INIT_BASE_RECEIVED", flow.work, flow.bytes);
        InitializationReceived(state);
        break;
    case ProtectionTransferKind::INIT_STATE:
        state.stateReceivedNs = at;
        Log(state, "INIT_STATE_RECEIVED", flow.work, flow.bytes);
        InitializationReceived(state);
        break;
    case ProtectionTransferKind::L1:
        state.records.at(flow.work).received = true;
        state.records.at(flow.work).receivedNs = at;
        Require(state.progress.ReceiveLocal(flow.work, at), "duplicate local receiver commit");
        ++state.summary.localCommits;
        Log(state, "L1_COMMITTED_LOCAL", flow.work, flow.bytes);
        TryBatch(state);
        break;
    case ProtectionTransferKind::REMOTE_BATCH:
        Log(state, "REMOTE_BATCH_RECEIVED", flow.work, flow.bytes);
        Later(state, state.progress.ReceiveRemote(flow.work, at), [this, &state] {
            Commit(state, false);
        });
        break;
    default:
        throw std::logic_error("recovery traffic is outside G2");
    }
}

void
CheckpointManager::InitializationReceived(State& state)
{
    if (state.baseReceivedNs < 0 || state.stateReceivedNs < 0)
        return;
    const auto commit =
        state.progress.ReceiveInitialization(state.baseReceivedNs, state.stateReceivedNs);
    Log(state, "INIT_MERGE_STARTED", state.initial);
    Later(state, commit, [this, &state] { Commit(state, true); });
}

void
CheckpointManager::ScheduleCapture(State& state)
{
    state.nextTarget.reset();
    if (state.futurePaused)
        return;
    auto next = state.layout.Next(Actual(state), state.triggered, state.config.deltaPermille);
    if (!next || *next >= state.layout.Work())
        return;
    const auto at =
        state.task.computeStartTimeNs + ComputeService::CalculateServiceTimeNs(*next, state.rate);
    state.captureEvent = Later(state, at, [this, &state, work = *next] {
        state.nextTarget.reset();
        Capture(state, work);
    });
    if (state.captureEvent.IsPending())
        state.nextTarget = next;
}

void
CheckpointManager::Capture(State& state, uint64_t work)
{
    const auto bytes = state.layout.RecordBytes(state.triggered, work);
    const auto generated = state.progress.Capture(work, Actual(state), Now());
    state.records.emplace(work, Record{state.triggered, work, bytes});
    state.triggered = work;
    Log(state, "L1_CAPTURED", work, bytes);
    Later(state, generated, [this, &state, work, bytes] {
        ++state.summary.generated;
        Log(state, "L1_GENERATED", work, bytes);
        const auto object =
            Reserve(state, state.config.localNode, StorageKind::LOCAL_RECORD, bytes, work);
        if (!object)
            return;
        state.records.at(work).object = *object;
        Queue(state,
              ProtectionTransferKind::L1,
              work,
              state.summary.primaryNode,
              state.config.localNode,
              bytes,
              work,
              *object);
    });
    ScheduleCapture(state);
}

void
CheckpointManager::TryBatch(State& state)
{
    if (state.futurePaused || state.batchInFlight || state.batchBlocked)
        return;
    const auto snapshot = state.progress.Current();
    uint64_t bytes = 0, work = 0;
    uint32_t count = 0;
    for (auto it = state.records.upper_bound(snapshot.remoteWork);
         it != state.records.end() && it->first <= snapshot.localWork;
         ++it)
    {
        Require(it->second.received, "batch skipped an unreceived local record");
        Require(it->second.bytes <= std::numeric_limits<uint64_t>::max() - bytes,
                "remote batch size overflow");
        bytes += it->second.bytes;
        work = it->first;
        if (++count == state.config.batchN)
            break;
    }
    if (count != state.config.batchN)
        return;
    const auto object =
        Reserve(state, state.config.remoteNode, StorageKind::REMOTE_BATCH, bytes, work);
    if (!object)
    {
        state.batchBlocked = true;
        return;
    }
    state.batchObject = *object;
    state.batchWork = work;
    state.batchInFlight = true;
    Log(state, "REMOTE_BATCH_STARTED", work, bytes);
    Queue(state,
          ProtectionTransferKind::REMOTE_BATCH,
          work,
          state.config.localNode,
          state.config.remoteNode,
          bytes,
          work,
          *object);
}

void
CheckpointManager::Commit(State& state, bool initialization)
{
    const uint64_t work = initialization ? state.initial : state.batchWork;
    const auto committed = state.progress.CommitRemote(Now());
    Require(committed && *committed == work, "remote progress/merge target mismatch");
    ++state.summary.remoteCommits;
    if (initialization)
        state.summary.initializationNs = Now();
    if (m_recoveryRetention)
    {
        Log(state,
            initialization ? "INIT_COMPLETE" : "REMOTE_COMMIT",
            work,
            state.layout.CommittedStateBytes(work, m_inputPolicy));
        if (initialization)
            Log(state, "ON", work);
        state.physicalCommit = std::pair{Now(), initialization};
        Later(state, Now() + 1, [this, &state, initialization] {
            if (state.physicalCommit)
                FinishPhysicalCommit(state, initialization);
        });
    }
    else
        FinishPhysicalCommit(state, initialization);
}

void
CheckpointManager::FinishPhysicalCommit(State& state, bool initialization)
{
    const uint64_t work = initialization ? state.initial : state.batchWork;
    auto& remote = *m_pools.at(state.config.remoteNode);
    Require(remote.Merge(state.baseObject,
                         initialization ? state.initObject : state.batchObject,
                         state.layout.CommittedStateBytes(work, m_inputPolicy)),
            "remote in-place merge failed");
    state.physicalCommit.reset();
    Log(state, initialization ? "INIT_COST_COMMITTED" : "REMOTE_COST_COMMITTED", work);
    if (!m_recoveryRetention)
    {
        Log(state,
            initialization ? "INIT_COMPLETE" : "REMOTE_COMMIT",
            work,
            state.layout.CommittedStateBytes(work, m_inputPolicy));
        if (initialization)
            Log(state, "ON", work);
    }
    if (initialization)
    {
        state.initObject = 0;
        state.initialized = true;
        if (m_initialized)
            m_initialized(state.summary.taskId);
        ScheduleCapture(state);
    }
    else
    {
        state.batchObject = 0;
        state.batchInFlight = false;
        auto& local = *m_pools.at(state.config.localNode);
        for (auto it = state.records.begin(); it != state.records.end() && it->first <= work;)
        {
            const auto record = it->second;
            Require(record.received && local.Release(record.object),
                    "covered local record missing");
            it = state.records.erase(it);
            Log(state,
                "LOCAL_CLEANUP",
                record.work,
                record.bytes,
                state.config.localNode,
                record.object);
        }
        TryBatch(state);
    }
}

bool
CheckpointManager::UpdateFutureConfiguration(uint64_t id, uint32_t delta, uint32_t n)
{
    Require(delta && delta <= 1000 && n && n <= 1000 / delta, "invalid future cadence");
    auto found = m_states.find(id);
    if (found == m_states.end() || !Live(*found->second) || !found->second->initialized)
        return false;
    auto& state = *found->second;
    const bool reschedule = state.futurePaused || state.config.deltaPermille != delta;
    state.config.deltaPermille = delta;
    state.config.batchN = n;
    state.futurePaused = false;
    state.batchBlocked = false;
    if (reschedule)
    {
        Simulator::Cancel(state.captureEvent);
        ScheduleCapture(state);
    }
    Log(state, "FREQUENCY_UPDATED");
    TryBatch(state);
    return true;
}

bool
CheckpointManager::PauseFutureProtection(uint64_t id)
{
    auto found = m_states.find(id);
    if (found == m_states.end() || !Live(*found->second) || !found->second->initialized)
        return false;
    auto& state = *found->second;
    state.futurePaused = true;
    Simulator::Cancel(state.captureEvent);
    state.nextTarget.reset();
    Log(state, "FREQUENCY_PAUSED");
    return true;
}

std::optional<CheckpointInventory>
CheckpointManager::Inventory(uint64_t id) const
{
    auto found = m_states.find(id);
    if (found == m_states.end())
        return std::nullopt;
    const auto& state = *found->second;
    CheckpointInventory result;
    result.config = state.config;
    result.progress = state.progress.Current();
    result.actual = Actual(state);
    result.triggered = state.triggered;
    const auto& remote = *m_pools.at(state.config.remoteNode);
    if (const auto base = remote.Find(state.baseObject))
        result.baseBytes = base->bytes;
    if (const auto batch = remote.Find(state.batchObject))
        result.batchBytes = batch->bytes;
    result.batchWork = state.batchWork;
    result.active = state.active;
    result.stopNs = state.summary.stopNs;
    result.initialized = state.initialized;
    result.paused = state.futurePaused;
    result.batchInFlight = state.batchInFlight;
    result.nextTarget = state.nextTarget;
    for (const auto& [work, record] : state.records)
        result.records.push_back({record.from, work, record.bytes,
                                  m_pools.at(state.config.localNode)->Find(record.object) != nullptr,
                                  record.received});
    return result;
}

void
CheckpointManager::Stop(State& state, const std::string& reason)
{
    if (!state.active)
        return;
    state.active = false;
    state.summary.stopNs = Now();
    state.summary.stopReason = reason;
    ReleaseInput(state.summary.taskId, reason);
    for (auto timer : state.timers)
        Simulator::Cancel(timer);
    state.progress.Stop();
    for (auto& [time, requests] : m_requests)
        std::erase_if(requests, [&](const auto& request) {
            return request.first.taskId == state.summary.taskId;
        });
    std::vector<uint64_t> transfers;
    for (const auto& flow : m_flows)
        if (flow.key.taskId == state.summary.taskId && flow.key.attemptGeneration == 0)
            transfers.push_back(flow.transferId);
    m_network->FinalizeTransfersIfActive(
        transfers,
        TransferTerminalState::CANCELLED,
        reason == "SIMULATION_ENDED" ? TransferTerminalReason::SIMULATION_ENDED
                                     : TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
    for (auto& [node, pool] : m_pools)
        pool->ReleaseTask(state.summary.taskId);
    state.records.clear();
    Log(state, "PROTECTION_STOP");
    if (m_assignmentObserver)
        m_assignmentObserver(state.summary.taskId, state.config.remoteNode, false);
}

void
CheckpointManager::OnTaskComputeComplete(AttemptKey attempt)
{
    if (attempt.generation != 0)
        return;
    const auto state = m_states.find(attempt.taskId);
    if (state == m_states.end())
        return;
    Log(*state->second, "COMPUTE_COMPLETE", state->second->layout.Work());
    Stop(*state->second, "COMPUTE_COMPLETE");
}

void
CheckpointManager::OnTaskTerminal(uint64_t id)
{
    const auto state = m_states.find(id);
    if (state != m_states.end())
        Stop(*state->second, "TASK_TERMINAL");
    ReleaseRecoveryState(id);
}

void
CheckpointManager::Finalize()
{
    for (auto& [id, state] : m_states)
    {
        Stop(*state, "SIMULATION_ENDED");
        ReleaseRecoveryState(id);
        state->physicalCommit.reset();
    }
    for (auto& [time, event] : m_flushEvents)
        Simulator::Cancel(event);
    m_flushEvents.clear();
    m_requests.clear();
}

std::vector<ProtectionTaskSummary>
CheckpointManager::Summaries() const
{
    std::vector<ProtectionTaskSummary> result;
    for (const auto& [id, state] : m_states)
    {
        auto row = state->summary;
        row.normalCostNs = (row.initGenerated + row.localGeneratedCostCount) * row.localCostNs +
                           (row.initCommitted + row.remoteCommittedCostCount) * row.remoteCostNs;
        row.localPeakBytes = m_pools.at(row.localNode)->TaskPeak(id);
        row.remotePeakBytes = m_pools.at(row.remoteNode)->TaskPeak(id);
        result.push_back(row);
    }
    return result;
}

bool
CheckpointManager::IsQuiescent() const
{
    for (const auto& [id, input] : m_inputs)
        if (input.localEvent.IsPending() || input.snapshot.pendingAdmission ||
            input.snapshot.stage == InputStage::IN_FLIGHT || input.snapshot.stage == InputStage::READY)
            return false;
    for (auto service : m_tasks->GetComputeServices())
        if (service->HasRecoveryReservation())
            return false;
    for (const auto& row : m_network->CollectSummaries())
        if (m_network->IsRuntimeTransfer(row.transferId) && row.transferState != "COMPLETED" &&
            row.transferState != "FAILED" && row.transferState != "CANCELLED")
            return false;
    for (const auto& [time, requests] : m_requests)
        if (!requests.empty())
            return false;
    for (const auto& [id, state] : m_states)
    {
        if (state->active || state->physicalCommit)
            return false;
        for (const auto& timer : state->timers)
            if (timer.IsPending())
                return false;
    }
    for (const auto& [node, pool] : m_pools)
        if (pool->Used() || pool->Reserved())
            return false;
    return true;
}

RecoverySnapshot
CheckpointManager::FreezeRecoverySnapshot(uint64_t id, int64_t at)
{
    RecoverySnapshot result;
    result.taskId = id;
    result.faultNs = at;
    result.input = InputStaging(id);
    if (result.input.stage == InputStage::READY && result.input.readyNs >= at)
        result.input.stage = InputStage::IN_FLIGHT;
    const auto task = std::find_if(m_tasks->GetTaskRuntimes().begin(),
                                   m_tasks->GetTaskRuntimes().end(),
                                   [id](const auto& t) { return t.definition.taskId == id; });
    Require(task != m_tasks->GetTaskRuntimes().end() && task->state == TASK_RUNNING && at == Now(),
            "snapshot requires the live primary at fault time");
    result.deadlineNs = task->computeDeadlineTimeNs;
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == task->definition.computeNodeId)
            result.actualWork = static_cast<uint64_t>(std::min<unsigned __int128>(
                task->definition.computeWorkUnits,
                static_cast<unsigned __int128>(at - task->computeStartTimeNs) *
                    service->GetComputeRateWorkUnitsPerSecond() / 1000000000));
    auto found = m_states.find(id);
    if (found == m_states.end() ||
        (!found->second->active && found->second->summary.stopReason != "QUIESCE_FOR_RECOVERY"))
        return result;
    auto& state = *found->second;
    if (state.physicalCommit && state.physicalCommit->first < at)
        FinishPhysicalCommit(state, state.physicalCommit->second);
    const auto before = state.progress.BeforeFault(at);
    result.phase = before.initialized ? "ON" : "INITIALIZING";
    result.localNode = state.config.localNode;
    result.remoteNode = state.config.remoteNode;
    result.localWork = before.localWork;
    result.remoteWork = before.remoteWork;
    result.localCostNs = state.summary.localCostNs;
    result.remoteCostNs = state.summary.remoteCostNs;
    result.pendingRemoteObject = state.batchObject;
    const auto batch =
        state.batchObject ? Pool(result.remoteNode).Find(state.batchObject) : nullptr;
    result.remoteMergePending =
        (batch && !batch->reserved) ||
        (!before.initialized && state.baseReceivedNs >= 0 && state.stateReceivedNs >= 0);
    if (before.initialized && m_tasks->IsSatelliteAvailable(result.remoteNode))
    {
        const auto base = Pool(result.remoteNode).Find(state.baseObject);
        Require(base && !base->reserved &&
                    base->bytes == state.layout.CommittedStateBytes(before.remoteWork, m_inputPolicy),
                "strict fault snapshot lost its physical remote version");
        result.remoteObject = state.baseObject;
        result.remoteBytes = base->bytes;
        for (const auto& [work, record] : state.records)
            if (m_tasks->IsSatelliteAvailable(result.localNode) && work > before.remoteWork &&
                work <= before.localWork && record.received && record.receivedNs < at)
            {
                const auto entry = Pool(result.localNode).Find(record.object);
                Require(entry && !entry->reserved && entry->bytes == record.bytes,
                        "strict fault snapshot lost a local tail record");
                result.localObjects.emplace(work, record.object);
                result.tailBytes += record.bytes;
            }
    }
    for (const auto& [work, record] : state.records)
        if (!record.received || record.receivedNs >= at)
        {
            ++result.pendingRecords;
            result.pendingLocalWorks.push_back(work);
        }
    for (const auto& flow : m_flows)
        if (flow.key.taskId == id && flow.key.kind != ProtectionTransferKind::PREFETCH_INPUT &&
            !m_network->IsTerminal(flow.transferId))
        {
            ++result.inFlightFlows;
            if (flow.key.kind == ProtectionTransferKind::L1)
                result.inFlightLocalTransfers.push_back(flow.transferId);
            else
                result.inFlightRemoteTransfers.push_back(flow.transferId);
        }
    Require(result.remoteWork <= result.localWork && result.localWork <= result.actualWork,
            "fault snapshot violates r <= l <= x");
    Log(state, "FAULT_SNAPSHOT", result.actualWork, result.tailBytes);
    return result;
}

void
CheckpointManager::QuiesceForRecovery(const RecoverySnapshot& snapshot)
{
    const auto found = m_states.find(snapshot.taskId);
    if (found == m_states.end())
        return;
    auto& state = *found->second;
    const auto input = InputStaging(snapshot.taskId);
    const bool keepInput = input.heldForRecovery &&
        (input.stage == InputStage::IN_FLIGHT || input.stage == InputStage::READY);
    if (!keepInput) ReleaseInput(snapshot.taskId, "QUIESCE_WITHOUT_INPUT_HANDOFF");
    state.active = false;
    state.summary.stopNs = Now();
    state.summary.stopReason = "QUIESCE_FOR_RECOVERY";
    for (auto timer : state.timers)
        Simulator::Cancel(timer);
    state.physicalCommit.reset();
    state.progress.Stop();
    for (auto& [time, requests] : m_requests)
        std::erase_if(requests, [&](const auto& item) {
            return item.first.taskId == snapshot.taskId && item.first.attemptGeneration == 0;
        });
    std::vector<uint64_t> transfers;
    for (const auto& flow : m_flows)
        if (flow.key.taskId == snapshot.taskId && flow.key.attemptGeneration == 0 &&
            !(keepInput && flow.key.kind == ProtectionTransferKind::PREFETCH_INPUT))
            transfers.push_back(flow.transferId);
    m_network->FinalizeTransfersIfActive(transfers,
                                         TransferTerminalState::CANCELLED,
                                         TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
    for (auto& [node, pool] : m_pools)
    {
        std::set<uint64_t> keep;
        if (keepInput && node == input.holderNode && input.objectId) keep.insert(input.objectId);
        if (node == snapshot.remoteNode && snapshot.remoteObject)
            keep.insert(snapshot.remoteObject);
        if (node == snapshot.localNode)
            for (const auto& [work, object] : snapshot.localObjects)
                keep.insert(object);
        pool->ReleaseTaskExcept(snapshot.taskId, keep);
    }
    Log(state, "QUIESCE_FOR_RECOVERY", snapshot.actualWork, snapshot.tailBytes);
    if (m_assignmentObserver && !snapshot.remoteObject)
        m_assignmentObserver(snapshot.taskId, state.config.remoteNode, false);
}

void
CheckpointManager::ReleaseRecoveryState(uint64_t id, bool retainInput)
{
    if (!retainInput) ReleaseInput(id, "RECOVERY_OWNERSHIP_RELEASED");
    const auto input = InputStaging(id);
    for (auto& [node, pool] : m_pools)
        if (retainInput && input.holderNode == node && input.objectId)
            pool->ReleaseTaskExcept(id, {input.objectId});
        else pool->ReleaseTask(id);
    const auto found = m_states.find(id);
    if (found != m_states.end() && m_assignmentObserver)
        m_assignmentObserver(id, found->second->config.remoteNode, false);
}

void
CheckpointManager::RecordRecoveryEvent(const RecoverySnapshot& s,
                                       const std::string& event,
                                       uint64_t bytes,
                                       uint64_t transferId)
{
    ProtectionEvent row;
    row.taskId = s.taskId;
    row.generation = 1;
    row.timeNs = Now();
    row.event = event;
    row.localNode = s.localNode;
    row.remoteNode = s.remoteNode;
    row.bytes = bytes;
    row.localWork = s.localWork;
    row.remoteWork = s.remoteWork;
    row.actualWork = s.actualWork;
    row.transferId = transferId;
    if (s.phase != "OFF")
    {
        row.localUsed = Pool(s.localNode).Used();
        row.localReserved = Pool(s.localNode).Reserved();
        row.remoteUsed = Pool(s.remoteNode).Used();
        row.remoteReserved = Pool(s.remoteNode).Reserved();
    }
    m_events.push_back(std::move(row));
}

void
CheckpointManager::QueueRecovery(ProtectionTransferKey key,
                                 uint32_t source,
                                 uint32_t destination,
                                 uint64_t bytes,
                                 uint64_t work,
                                 uint64_t object,
                                 std::function<bool()> live,
                                 std::function<void(uint64_t)> registered)
{
    Require(key.attemptGeneration == 1 && source != destination && bytes && live && registered,
            "recovery network request requires real cross-node bytes and attempt guards");
    QueueOwnedTransfer(key, source, destination, bytes, work, object, std::move(live), std::move(registered));
}

void CheckpointManager::QueueOwnedTransfer(ProtectionTransferKey key,
    uint32_t source, uint32_t destination, uint64_t bytes, uint64_t work, uint64_t object,
    std::function<bool()> live, std::function<void(uint64_t)> registered)
{
    Require((key.attemptGeneration == 1 || (key.attemptGeneration == 0 &&
             key.kind == ProtectionTransferKind::PREFETCH_INPUT)) &&
             source != destination && bytes && live && registered,
             "owned transfer requires a valid identity, cross-node payload and guard");
    const auto time = Now();
    Require(m_requests[time]
                .emplace(key,
                         Request{key,
                                 source,
                                 destination,
                                 bytes,
                                 work,
                                 object,
                                 time,
                                 std::move(live),
                                 std::move(registered)})
                .second,
            "duplicate recovery request");
    if (!m_flushEvents.contains(time))
        m_flushEvents.emplace(
            time, Simulator::Schedule(NanoSeconds(1), &CheckpointManager::Flush, this, time));
}
} // namespace ns3::protection
