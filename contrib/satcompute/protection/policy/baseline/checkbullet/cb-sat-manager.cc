/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-manager.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ns3::protection::checkbullet
{
namespace
{
int64_t Now() { return Simulator::Now().GetNanoSeconds(); }
void Require(bool value, const char* message)
{
    if (!value) throw std::logic_error(message);
}
uint64_t MaximumId(Ptr<TaskCoordinator> tasks)
{
    if (!tasks) throw std::invalid_argument("CB requires task services");
    uint64_t maximum = 0;
    for (const auto& plan : tasks->GetTransferEngine()->GetPlans())
        maximum = std::max(maximum, plan.transferId);
    return maximum;
}
} // namespace

const char*
CbFlowName(CbFlowKind kind)
{
    switch (kind)
    {
    case CbFlowKind::INIT_INPUT: return "CB_INIT_INPUT";
    case CbFlowKind::INIT_FULL: return "CB_INIT_FULL";
    case CbFlowKind::DELTA: return "CB_DELTA";
    case CbFlowKind::RELOCATE_INPUT: return "CB_RELOCATE_INPUT";
    case CbFlowKind::RELOCATE_FULL: return "CB_RELOCATE_FULL";
    case CbFlowKind::RELOCATE_LOG: return "CB_RELOCATE_LOG";
    case CbFlowKind::FALLBACK_INPUT: return "CB_FALLBACK_INPUT";
    case CbFlowKind::RESULT: return "RESULT";
    }
    throw std::invalid_argument("unknown CB flow kind");
}

CbSatManager::Normal::Normal(const TaskRuntime& runtime, uint64_t rate, double mtbf)
    : task(runtime), layout(runtime.definition)
{
    summary.task = task.definition.taskId;
    summary.primary = task.definition.computeNodeId;
    summary.inputBytes = task.definition.inputBytes;
    summary.work = layout.Work();
    summary.variableBytes = layout.VariableBytes();
    summary.rate = rate;
    summary.startNs = task.computeStartTimeNs;
    summary.interval = SolveInterval(layout, rate, mtbf);
    const auto costs = GetProtectionCosts(layout.VariableBytes());
    summary.localCostNs = costs.localNs;
    summary.remoteCostNs = costs.remoteNs;
}

CbSatManager::CbSatManager(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                           uint64_t capacity, int64_t stopNs, double mtbf,
                           PlacementPolicy& placement, PlacementLoadLedger& loads)
    : m_tasks(tasks), m_topology(topology), m_network(tasks ? tasks->GetTransferEngine() : nullptr),
      m_stopNs(stopNs), m_mtbf(mtbf), m_placement(placement), m_loads(loads),
      m_ids(MaximumId(tasks))
{
    if (stopNs <= 0 || std::isnan(mtbf) || mtbf <= 0)
        throw std::invalid_argument("invalid CB duration/MTBF");
    for (auto service : tasks->GetComputeServices())
    {
        const auto node = service->GetNodeId();
        Require(topology.HasSatelliteId(node), "unknown CB compute satellite");
        m_pools.emplace(node, std::make_unique<BackupStoragePool>(capacity));
        m_roles.emplace(node, std::map<uint64_t, std::string>{});
        m_loads.RegisterNode(node);
    }
    for (const auto& task : tasks->GetTaskRuntimes()) TaskStateAdapter{task.definition};
    tasks->ConnectTaskObserver(MakeCallback(&CbSatManager::OnTask, this));
}

CbSatManager::~CbSatManager()
{
    m_tasks->DisconnectTaskObserver(MakeCallback(&CbSatManager::OnTask, this));
    for (auto& [id, normal] : m_normal)
        for (auto event : normal->timers) Simulator::Cancel(event);
    for (auto& [time, event] : m_flushes) Simulator::Cancel(event);
    for (auto& [node, event] : m_changed) Simulator::Cancel(event);
}

Ptr<ComputeService> CbSatManager::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node) return service;
    throw std::invalid_argument("unknown CB service");
}

uint64_t CbSatManager::Actual(const Normal& normal) const
{
    const auto elapsed = std::max<int64_t>(0, Now() - normal.summary.startNs);
    return static_cast<uint64_t>(std::min<unsigned __int128>(normal.layout.Work(),
        static_cast<unsigned __int128>(elapsed) * normal.summary.rate / 1000000000));
}

bool CbSatManager::Live(const Normal& normal) const
{
    if (!normal.live || normal.task.state != TASK_RUNNING || Now() >= m_stopNs)
        return false;
    const auto service = Service(normal.summary.primary);
    return service->HasRunningTask() && service->GetRunningTaskId() == normal.summary.task &&
           Actual(normal) < normal.layout.Work();
}

void CbSatManager::Later(Normal& normal, int64_t at, std::function<void()> callback)
{
    Require(at >= Now(), "CB callback in the past");
    if (at >= m_stopNs) return;
    normal.timers.push_back(Simulator::Schedule(NanoSeconds(at - Now()),
        [this, &normal, callback = std::move(callback)] {
            if (Live(normal)) callback();
        }));
}

void CbSatManager::OnTask(const TaskEventRecord& event)
{
    if (event.toState == TASK_RUNNING && !m_normal.contains(event.taskId))
    {
        const auto& tasks = m_tasks->GetTaskRuntimes();
        const auto task = std::find_if(tasks.begin(), tasks.end(), [&](const auto& row) {
            return row.definition.taskId == event.taskId;
        });
        Require(task != tasks.end(), "CB task disappeared");
        auto owned = std::make_unique<Normal>(*task,
            Service(event.nodeId)->GetComputeRateWorkUnitsPerSecond(), m_mtbf);
        auto& normal = *owned;
        m_normal.emplace(event.taskId, std::move(owned));
        Log(event.taskId, normal.summary.interval.reason, event.nodeId);
        ScheduleTarget(normal);
    }
    const auto found = m_normal.find(event.taskId);
    if (found == m_normal.end()) return;
    if (event.toState == TASK_RESULT_TRANSFERRING && event.fromState == TASK_RUNNING)
        Stop(*found->second, "COMPUTE_COMPLETE");
    if (IsTerminalTaskState(event.toState)) ReleaseTask(event.taskId, "TASK_TERMINAL");
}

void CbSatManager::ScheduleTarget(Normal& normal)
{
    if (!Live(normal) || normal.nextTarget >= normal.summary.interval.targets.size()) return;
    const auto target = normal.summary.interval.targets.at(normal.nextTarget);
    const auto at = normal.summary.startNs +
        ComputeService::CalculateServiceTimeNs(target.work, normal.summary.rate);
    Later(normal, std::max(Now(), at), [this, &normal, target] {
        ++normal.nextTarget;
        Boundary(normal, target);
        ScheduleTarget(normal);
    });
}

uint64_t CbSatManager::Occupied(uint32_t node, uint64_t task) const
{
    uint64_t total = 0;
    for (const auto& [object, role] : m_roles.at(node))
    {
        const auto entry = m_pools.at(node)->Find(object);
        Require(entry, "CB storage role without physical object");
        if (entry->taskId == task) total += entry->bytes;
    }
    return total;
}

uint64_t CbSatManager::Quota(uint32_t node, uint64_t task) const
{
    std::map<uint64_t, uint64_t> owners;
    for (const auto& [object, role] : m_roles.at(node))
    {
        const auto entry = m_pools.at(node)->Find(object);
        Require(entry, "CB quota object missing");
        owners[entry->taskId] += entry->bytes;
    }
    for (const auto& [id, normal] : m_normal)
        if ((normal->live || normal->retained) && normal->summary.backup == node)
            owners.try_emplace(id, 0);
    owners.try_emplace(task, 0);
    return ShareStorage(m_pools.at(node)->Capacity(), owners).at(task);
}

CbThreshold CbSatManager::Threshold(const Normal& normal, uint32_t node, uint64_t initial) const
{
    const auto& layout = normal.layout;
    std::vector<uint64_t> logs;
    uint64_t held = Occupied(node, normal.summary.task);
    uint64_t previous = initial;
    if (normal.state)
    {
        const auto snapshot = normal.state->At(Now());
        previous = snapshot.rootReady ? snapshot.rootWork :
                   normal.state->Records().begin()->second.key.toWork;
        for (const auto& [sequence, record] : normal.state->Records())
        {
            if (record.key.toWork <= previous) continue;
            logs.push_back(record.bytes);
            const auto object = normal.objects.find(sequence);
            if (object != normal.objects.end() && object->second.destination)
            {
                const auto entry = m_pools.at(node)->Find(object->second.destination);
                if (entry) held -= entry->bytes;
            }
            previous = record.key.toWork;
        }
    }
    else
        held += normal.summary.inputBytes + layout.StateBytes(initial) + layout.HeaderBytes();
    for (size_t index = normal.nextTarget; index < normal.summary.interval.targets.size(); ++index)
    {
        const auto& target = normal.summary.interval.targets.at(index);
        if (target.work > previous)
        {
            logs.push_back(layout.RecordBytes(previous, target.work));
            previous = target.work;
        }
    }
    const auto budget = std::max<int64_t>(0, normal.task.computeDeadlineTimeNs -
        normal.summary.startNs - ComputeService::CalculateServiceTimeNs(layout.Work(),
                                                                       normal.summary.rate));
    return SolveThreshold(Quota(node, normal.summary.task), held, logs, budget,
                          {0, 0, normal.summary.remoteCostNs, false});
}

bool CbSatManager::Select(Normal& normal, CbTarget target)
{
    const auto task = normal.summary.task;
    PlacementContext context;
    context.primaryNode = normal.summary.primary;
    for (auto service : m_tasks->GetComputeServices())
    {
        const auto node = service->GetNodeId();
        if (node == context.primaryNode) continue;
        context.candidates.push_back({node,
            m_tasks->IsComputeAvailable(node) && m_tasks->IsSatelliteAvailable(node),
            service->IsIdle(), !m_topology.GetEcmpRouteCandidates(context.primaryNode, node).empty(),
            false, service->GetQueueSize(), m_pools.at(node)->Free(),
            m_loads.Get(node).activeBackup, m_loads.Get(node).activeRecovery});
    }
    const uint64_t full = normal.layout.StateBytes(target.work) + normal.layout.HeaderBytes();
    auto reason = [&](uint32_t node) -> std::string {
        const auto path = m_network->EstimateAdmissiblePath(context.primaryNode, node);
        if (!path.reachable || !path.admissible) return "NO_ADMISSIBLE_PATH";
        if (full > Quota(context.primaryNode, task) - Occupied(context.primaryNode, task))
            return "SOURCE_CACHE_FULL";
        const auto quota = Quota(node, task);
        if (normal.summary.inputBytes > quota || full > quota - normal.summary.inputBytes)
            return "NO_STORAGE_SHARE";
        const auto threshold = Threshold(normal, node, target.work);
        if (threshold.naturalLimit && !threshold.feasible) return "CHECKPOINT_INFEASIBLE";
        return "";
    };
    const auto node = m_placement.SelectBackupNode(context, [&](auto id) { return reason(id).empty(); });
    CbDecision decision;
    decision.task = task;
    decision.decision = ++normal.decision;
    decision.timeNs = Now();
    decision.work = target.work;
    decision.backup = node;
    decision.reason = node ? reason(*node) : "NO_BACKUP_CANDIDATE";
    if (node)
    {
        decision.quota = Quota(*node, task);
        decision.occupied = Occupied(*node, task);
        decision.threshold = Threshold(normal, *node, target.work);
    }
    m_placement.RecordSelection({task, Now(), context.primaryNode, std::nullopt, node,
        decision.reason.empty() ? "ACCEPTED" : "REJECTED",
        decision.reason.empty() ? "INITIALIZATION_PENDING" : decision.reason});
    m_decisions.push_back(decision);
    if (!node || !decision.reason.empty())
    {
        Log(task, "BOUNDARY_ADMISSION_REJECTED", node.value_or(context.primaryNode),
            0, decision.decision, target.work, 0, 0, decision.reason);
        return false;
    }
    normal.summary.backup = node;
    normal.state = std::make_unique<CbState>(normal.task.definition, 0, *node);
    m_loads.Assignment(task, *node, true, Now());
    Changed(*node);
    Log(task, "BACKUP_ASSIGNED", *node, 0, decision.decision, target.work);
    return true;
}

void CbSatManager::Boundary(Normal& normal, CbTarget target)
{
    if (!normal.state && !Select(normal, target)) return;
    // Failed transmissions keep their original sequence and captured data until a later boundary.
    normal.retryAtBoundary.clear();
    SendPending(normal);
    const auto& records = normal.state->Records();
    const auto previous = records.empty() ? 0 : records.rbegin()->second.key.toWork;
    const auto bytes = records.empty() ? normal.state->FullBytes(target.work) :
                                       normal.layout.RecordBytes(previous, target.work);
    auto source = Reserve(normal.summary.task, normal.summary.primary, "SOURCE_CACHE", bytes);
    if (!source)
    {
        Log(normal.summary.task, "BOUNDARY_SKIPPED_SOURCE_FULL", normal.summary.primary,
            0, 0, target.work, bytes);
        return;
    }
    const auto record = normal.state->Capture(target.work, Actual(normal), Now());
    normal.objects.emplace(record.key.sequence, RecordObjects{*source, 0, false, false});
    Log(normal.summary.task, "CAPTURE", normal.summary.primary, *source,
        record.key.sequence, target.work, record.bytes);
    Later(normal, record.generatedNs, [this, &normal, sequence = record.key.sequence] {
        auto& objects = normal.objects.at(sequence);
        CommitObject(normal.summary.primary, objects.source);
        objects.generated = true;
        ++normal.summary.generated;
        normal.summary.normalCostNs += normal.summary.localCostNs;
        const auto& row = normal.state->Records().at(sequence);
        Log(normal.summary.task, "GENERATED", normal.summary.primary, objects.source,
            sequence, row.key.toWork, row.bytes);
        SendPending(normal);
    });
}

void CbSatManager::SendPending(Normal& normal)
{
    if (!Live(normal) || !normal.state) return;
    const auto task = normal.summary.task;
    const auto node = *normal.summary.backup;
    if (!normal.state->At(Now()).inputReady && !normal.inputSending &&
        !normal.retryAtBoundary.contains(0))
    {
        auto object = Reserve(task, node, "INPUT", normal.summary.inputBytes);
        if (object)
        {
            normal.inputObject = *object;
            normal.inputSending = true;
            Queue(task, 0, CbFlowKind::INIT_INPUT, 0, normal.summary.primary, node,
                normal.summary.inputBytes, *object, [this, &normal] { return Live(normal); },
                [this, &normal](auto id, bool ok) { Received(normal, 0, id, ok); });
        }
    }
    for (auto& [sequence, objects] : normal.objects)
    {
        const auto& row = normal.state->Records().at(sequence);
        if (!objects.generated || objects.sending || row.receivedNs >= 0 ||
            normal.retryAtBoundary.contains(sequence)) continue;
        if (!objects.destination)
        {
            auto reserved = Reserve(task, node, sequence == 1 ? "FULL" : "LOG", row.bytes);
            if (!reserved) continue;
            objects.destination = *reserved;
        }
        objects.sending = true;
        Queue(task, 0, sequence == 1 ? CbFlowKind::INIT_FULL : CbFlowKind::DELTA,
            sequence, normal.summary.primary, node, row.bytes, objects.destination,
            [this, &normal] { return Live(normal); },
            [this, &normal, sequence](auto id, bool ok) { Received(normal, sequence, id, ok); });
    }
}

void CbSatManager::Received(Normal& normal, uint64_t sequence, uint64_t transfer, bool completed)
{
    const auto task = normal.summary.task;
    const auto node = *normal.summary.backup;
    auto& object = sequence == 0 ? normal.inputObject : normal.objects.at(sequence).destination;
    (sequence == 0 ? normal.inputSending : normal.objects.at(sequence).sending) = false;
    if (!completed)
    {
        ReleaseObject(node, object, "TRANSFER_FAILED");
        object = 0;
        normal.retryAtBoundary.insert(sequence);
        Log(task, "RETRY_AT_NEXT_BOUNDARY", node, 0, sequence, 0, 0, transfer);
        return;
    }
    CommitObject(node, object);
    if (sequence == 0)
        Require(normal.state->ReceiveInput(node, normal.summary.inputBytes, Now()),
                "invalid CB INPUT receipt");
    else
    {
        const auto& row = normal.state->Records().at(sequence);
        Require(normal.state->Receive(row.key, node, Now()), "invalid CB state receipt");
        ReleaseObject(normal.summary.primary, normal.objects.at(sequence).source, "RECEIPT_ACK");
        normal.objects.at(sequence).source = 0;
        if (sequence == 1)
        {
            normal.rootObject = object;
            normal.initializationScheduled = true;
            Later(normal, Now() + normal.summary.remoteCostNs, [this, &normal] {
                Require(normal.state->CommitInitial(Now()), "CB root commit failed");
                normal.physicalCommit = Now();
                Log(normal.summary.task, "ROOT_COMMIT", *normal.summary.backup, normal.rootObject,
                    1, normal.state->At(Now()).rootWork);
                Later(normal, Now() + 1, [this, &normal] { FinishPhysicalCommit(normal); });
            });
        }
    }
    Log(task, "RECEIVED", node, object, sequence, 0, 0, transfer);
    if (normal.state->At(Now()).Protected() && normal.summary.initializedNs < 0)
    {
        normal.summary.initializedNs = Now();
        Log(task, "INITIALIZED", node);
    }
    TryMerge(normal);
}

void CbSatManager::TryMerge(Normal& normal)
{
    if (!Live(normal) || !normal.state || normal.merging || normal.physicalCommit) return;
    const auto snapshot = normal.state->At(Now());
    if (!snapshot.rootReady || snapshot.logSequences.empty()) return;
    const auto node = *normal.summary.backup;
    const auto threshold = Threshold(normal, node);
    m_decisions.push_back({normal.summary.task, ++normal.decision, snapshot.recoverableWork,
        Quota(node, normal.summary.task), Occupied(node, normal.summary.task), Now(),
        node, threshold, "X_REEVALUATED"});
    // A share contraction may require immediate compaction of already owned logs.
    // X=0 does not manufacture a checkpoint; it only releases space from valid existing objects.
    if (threshold.value && snapshot.logSequences.size() < threshold.value) return;
    normal.mergeSequence = snapshot.logSequences.back();
    normal.mergeLogs = snapshot.logSequences;
    const auto ready = normal.state->BeginMerge(normal.mergeSequence, Now());
    Require(ready.has_value(), "CB contiguous merge rejected");
    normal.merging = true;
    Log(normal.summary.task, "MERGE_START", node, normal.rootObject,
        normal.mergeSequence, snapshot.recoverableWork);
    Later(normal, *ready, [this, &normal] {
        Require(normal.state->CommitMerge(Now()), "CB merge commit rejected");
        normal.physicalCommit = Now();
        Log(normal.summary.task, "MERGE_COMMIT", *normal.summary.backup,
            normal.rootObject, normal.mergeSequence, normal.state->At(Now()).rootWork);
        Later(normal, Now() + 1, [this, &normal] { FinishPhysicalCommit(normal); });
    });
}

void CbSatManager::FinishPhysicalCommit(Normal& normal)
{
    if (!normal.physicalCommit) return;
    const auto node = *normal.summary.backup;
    if (normal.merging)
    {
        // No simulator event can interleave this prevalidated in-place object transaction.
        for (auto sequence : normal.mergeLogs)
        {
            const auto object = normal.objects.at(sequence).destination;
            const auto entry = m_pools.at(node)->Find(object);
            Require(entry && !entry->reserved && entry->taskId == normal.summary.task,
                    "CB merge lacks retained received log");
        }
        for (auto sequence : normal.mergeLogs)
        {
            auto& object = normal.objects.at(sequence).destination;
            const auto& row = normal.state->Records().at(sequence);
            Require(m_pools.at(node)->Merge(normal.rootObject, object,
                    normal.state->FullBytes(row.key.toWork)), "CB physical merge failed");
            m_roles.at(node).erase(object);
            Log(normal.summary.task, "LOG_MERGED", node, object, sequence, row.key.toWork);
            object = 0;
        }
        normal.mergeLogs.clear();
        normal.merging = false;
        ++normal.summary.merges;
    }
    else
        ++normal.summary.initialCommits;
    normal.summary.normalCostNs += normal.summary.remoteCostNs;
    normal.physicalCommit.reset();
    Log(normal.summary.task, "REMOTE_COST_COMMITTED", node, normal.rootObject);
    if (normal.state->At(Now()).Protected() && normal.summary.initializedNs < 0)
    {
        normal.summary.initializedNs = Now() - 1;
        Log(normal.summary.task, "INITIALIZED", node);
    }
    Changed(node);
    TryMerge(normal);
}

std::optional<uint64_t> CbSatManager::Reserve(uint64_t task, uint32_t node,
                                            const std::string& role, uint64_t bytes)
{
    // Quota is an ownership-floor plan, and the real pool remains the final authority.
    const auto occupied = Occupied(node, task);
    const auto quota = Quota(node, task);
    if (bytes > quota - occupied)
    {
        Log(task, "STORAGE_SHARE_REJECTED", node, 0, 0, 0, bytes, 0, role);
        return std::nullopt;
    }
    auto object = m_pools.at(node)->TryReserve(task,
        role == "SOURCE_CACHE" ? StorageKind::LOCAL_RECORD :
        role == "LOG" ? StorageKind::REMOTE_BATCH : StorageKind::REMOTE_STATE, bytes);
    if (!object)
    {
        Log(task, "STORAGE_CAPACITY_REJECTED", node, 0, 0, 0, bytes, 0, role);
        return std::nullopt;
    }
    m_roles.at(node).emplace(*object, role);
    uint64_t total = 0;
    for (const auto& [id, pool] : m_pools) total += pool->Used() + pool->Reserved();
    m_globalPeak = std::max(m_globalPeak, total);
    Log(task, "STORAGE_RESERVED", node, *object, 0, 0, bytes, 0, role);
    Changed(node);
    return object;
}

void CbSatManager::CommitObject(uint32_t node, uint64_t object)
{
    const auto entry = m_pools.at(node)->Find(object);
    Require(entry && entry->reserved, "CB commit without reservation");
    const auto owner = entry->taskId;
    Require(m_pools.at(node)->CommitReservation(object), "CB object commit failed");
    Log(owner, "STORAGE_USED", node, object);
    Changed(node);
}

void CbSatManager::ReleaseObject(uint32_t node, uint64_t object, const std::string& reason)
{
    if (!object) return;
    const auto entry = m_pools.at(node)->Find(object);
    if (!entry) return;
    const auto task = entry->taskId;
    const auto bytes = entry->bytes;
    const auto role = m_roles.at(node).at(object);
    Require(entry->reserved ? m_pools.at(node)->ReleaseReservation(object) :
                             m_pools.at(node)->Release(object), "CB release failed");
    m_roles.at(node).erase(object);
    Log(task, reason, node, object, 0, 0, bytes, 0, role);
    Changed(node);
}

void CbSatManager::Changed(uint32_t node)
{
    if (Now() + 1 >= m_stopNs || m_changed.contains(node)) return;
    m_changed.emplace(node, Simulator::Schedule(NanoSeconds(1), [this, node] {
        m_changed.erase(node);
        for (auto& [task, normal] : m_normal)
            if (normal->summary.backup == node) TryMerge(*normal);
    }));
}

void CbSatManager::Queue(uint64_t task, uint64_t generation, CbFlowKind kind, uint64_t sequence,
                        uint32_t source, uint32_t destination, uint64_t bytes, uint64_t object,
                        std::function<bool()> live,
                        std::function<void(uint64_t, bool)> terminal)
{
    Require(bytes && source != destination, "CB network queue requires real positive-byte cross-node flow");
    if (object)
    {
        const auto entry = m_pools.at(destination)->Find(object);
        Require(entry && entry->reserved && entry->taskId == task && entry->bytes == bytes,
                "CB flow lacks exact destination reservation");
    }
    const auto time = Now();
    CbFlow flow{task, generation, sequence, 0, bytes, object, source, destination, kind, time};
    Require(m_requests[time].emplace(RequestKey{task, generation, kind, sequence},
        Request{flow, std::move(live), std::move(terminal)}).second, "duplicate CB flow request");
    Log(task, "TRANSFER_REQUESTED", destination, object, sequence, 0, bytes, 0, CbFlowName(kind));
    if (!m_flushes.contains(time))
        m_flushes.emplace(time, Simulator::Schedule(NanoSeconds(1), &CbSatManager::Flush, this, time));
}

void CbSatManager::Flush(int64_t requestedNs)
{
    auto bucket = m_requests.extract(requestedNs);
    m_flushes.erase(requestedNs);
    if (bucket.empty()) return;
    for (auto& [key, request] : bucket.mapped())
    {
        if (!request.live()) continue;
        auto& flow = request.flow;
        const auto id = m_ids.Next();
        NetworkTransfer plan;
        plan.transferId = id;
        plan.sourceSatelliteId = flow.source;
        plan.destinationSatelliteId = flow.destination;
        plan.sizeBytes = flow.bytes;
        try { m_network->RegisterRuntimePlan(plan, flow.kind == CbFlowKind::RESULT); }
        catch (const NetworkTransferConfigError&)
        {
            Log(flow.task, "TRANSFER_REGISTRATION_REJECTED", flow.destination, flow.object,
                flow.sequence, 0, flow.bytes, 0, CbFlowName(flow.kind));
            request.terminal(0, false);
            continue;
        }
        flow.transfer = id;
        flow.registeredNs = Now();
        m_flowIndexes.emplace(id, m_flows.size());
        m_flows.push_back(flow);
        m_terminals.emplace(id, [live = request.live, terminal = request.terminal](auto fid, bool ok) {
            if (live()) terminal(fid, ok);
        });
        m_network->SetTerminalObserver(id, MakeCallback(&CbSatManager::TransferTerminal, this));
        Log(flow.task, "TRANSFER_REGISTERED", flow.destination, flow.object,
            flow.sequence, 0, flow.bytes, id, CbFlowName(flow.kind));
        m_network->StartTransferNow(id);
    }
}

void CbSatManager::TransferTerminal(uint64_t transfer, int64_t at)
{
    auto& flow = m_flows.at(m_flowIndexes.at(transfer));
    flow.terminalNs = at;
    flow.completed = m_network->IsCompleted(transfer);
    Log(flow.task, flow.completed ? "TRANSFER_COMPLETE" : "TRANSFER_TERMINAL", flow.destination,
        flow.object, flow.sequence, 0, flow.bytes, transfer, CbFlowName(flow.kind));
    auto callback = m_terminals.extract(transfer);
    if (!callback.empty()) callback.mapped()(transfer, flow.completed);
}

void CbSatManager::CancelFlows(uint64_t task, uint64_t generation)
{
    for (auto& [time, requests] : m_requests)
        std::erase_if(requests, [=](const auto& entry) {
            return entry.second.flow.task == task && entry.second.flow.generation == generation;
        });
    std::vector<uint64_t> ids;
    for (const auto& flow : m_flows)
        if (flow.task == task && flow.generation == generation && !m_network->IsTerminal(flow.transfer))
            ids.push_back(flow.transfer);
    m_network->FinalizeTransfersIfActive(ids, TransferTerminalState::CANCELLED,
                                         TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
}

void CbSatManager::Log(uint64_t task, const std::string& event, uint32_t node,
                      uint64_t object, uint64_t sequence, uint64_t work,
                      uint64_t bytes, uint64_t transfer, const std::string& role)
{
    CbEvent row;
    row.task = task; row.event = event; row.timeNs = Now(); row.node = node;
    row.object = object; row.sequence = sequence; row.work = work; row.bytes = bytes;
    row.transfer = transfer; row.role = role;
    if (const auto state = Snapshot(task))
    {
        row.root = state->rootWork;
        row.recoverable = state->recoverableWork;
    }
    if (m_pools.contains(node))
    {
        row.used = m_pools.at(node)->Used(); row.reserved = m_pools.at(node)->Reserved();
        if (row.role.empty() && m_roles.at(node).contains(object)) row.role = m_roles.at(node).at(object);
    }
    m_events.push_back(row);
}

std::optional<CbSnapshot> CbSatManager::Snapshot(uint64_t task) const
{
    const auto found = m_normal.find(task);
    if (found == m_normal.end() || !found->second->state) return std::nullopt;
    return found->second->state->At(Now());
}

const CbState* CbSatManager::State(uint64_t task) const
{
    const auto found = m_normal.find(task);
    return found == m_normal.end() ? nullptr : found->second->state.get();
}

CbRecoverySnapshot CbSatManager::Freeze(uint64_t task, int64_t faultNs)
{
    auto& normal = *m_normal.at(task);
    Require(faultNs == Now(), "CB fault snapshot must be causal");
    // A same-ns physical callback might have a later UID; commit older logical transactions first.
    if (normal.physicalCommit && *normal.physicalCommit < faultNs) FinishPhysicalCommit(normal);
    CbRecoverySnapshot snapshot;
    snapshot.state.taskId = task;
    snapshot.actualWork = Actual(normal);
    snapshot.cutoffNs = faultNs;
    snapshot.deadlineNs = normal.task.computeDeadlineTimeNs;
    if (!normal.state) return snapshot;
    snapshot.state = normal.state->BeforeFault(faultNs);
    if (snapshot.state.inputReady) snapshot.inputObject = normal.inputObject;
    if (snapshot.state.rootReady)
    {
        snapshot.rootObject = normal.rootObject;
        for (auto sequence : snapshot.state.logSequences)
            snapshot.logObjects.emplace(sequence, normal.objects.at(sequence).destination);
    }
    return snapshot;
}

void CbSatManager::RetainForRecovery(const CbRecoverySnapshot& snapshot)
{
    auto& normal = *m_normal.at(snapshot.state.taskId);
    Stop(normal, "FAULT_HANDOFF", true);
    std::set<uint64_t> retained;
    if (snapshot.inputObject) retained.insert(snapshot.inputObject);
    if (snapshot.rootObject) retained.insert(snapshot.rootObject);
    for (const auto& [sequence, object] : snapshot.logObjects) retained.insert(object);
    for (const auto& [node, pool] : m_pools)
    {
        std::vector<uint64_t> discard;
        for (const auto& [object, role] : m_roles.at(node))
        {
            const auto entry = pool->Find(object);
            if (entry->taskId == snapshot.state.taskId &&
                (node != snapshot.state.backupNode || !retained.contains(object))) discard.push_back(object);
        }
        for (auto object : discard) ReleaseObject(node, object, "FAULT_DISCARD");
    }
}

void CbSatManager::Stop(Normal& normal, const std::string& reason, bool retain)
{
    if (!normal.live) return;
    if (normal.physicalCommit && *normal.physicalCommit < Now()) FinishPhysicalCommit(normal);
    normal.live = false;
    normal.retained = retain;
    normal.summary.stoppedNs = Now();
    normal.summary.stopReason = reason;
    if (normal.state)
    {
        const auto snapshot = normal.state->At(Now());
        normal.summary.rootWork = snapshot.rootWork;
        normal.summary.recoverableWork = snapshot.recoverableWork;
        normal.state->Stop();
    }
    for (auto event : normal.timers) Simulator::Cancel(event);
    if (normal.merging) Log(normal.summary.task, "MERGE_CANCELLED", *normal.summary.backup);
    normal.physicalCommit.reset();
    CancelFlows(normal.summary.task, 0);
    Log(normal.summary.task, reason, normal.summary.primary);
    if (!retain) ReleaseTask(normal.summary.task, reason);
}

void CbSatManager::ReleaseTask(uint64_t task, const std::string& reason)
{
    const auto found = m_normal.find(task);
    if (found != m_normal.end())
    {
        auto& normal = *found->second;
        if (normal.live) { Stop(normal, reason); return; }
        if (normal.summary.backup && (normal.retained || m_loads.Get(*normal.summary.backup).activeBackup))
            m_loads.Assignment(task, *normal.summary.backup, false, Now());
        normal.retained = false;
    }
    for (const auto& [node, pool] : m_pools)
    {
        std::vector<uint64_t> discard;
        for (const auto& [object, role] : m_roles.at(node))
            if (pool->Find(object)->taskId == task) discard.push_back(object);
        for (auto object : discard) ReleaseObject(node, object, reason);
    }
}

std::vector<CbTaskSummary> CbSatManager::Summaries() const
{
    std::vector<CbTaskSummary> out;
    for (const auto& [task, normal] : m_normal)
    {
        auto row = normal->summary;
        if (const auto snapshot = Snapshot(task))
        {
            row.rootWork = snapshot->rootWork;
            row.recoverableWork = snapshot->recoverableWork;
        }
        out.push_back(row);
    }
    return out;
}

void CbSatManager::Finalize()
{
    for (auto& [task, normal] : m_normal) ReleaseTask(task, "SIMULATION_END");
    for (auto& [time, event] : m_flushes) Simulator::Cancel(event);
    for (auto& [node, event] : m_changed) Simulator::Cancel(event);
    m_requests.clear(); m_flushes.clear(); m_changed.clear();
}

bool CbSatManager::IsQuiescent() const
{
    for (const auto& [task, normal] : m_normal)
        if (normal->live || normal->retained) return false;
    for (const auto& [node, pool] : m_pools)
        if (pool->Used() || pool->Reserved()) return false;
    return m_requests.empty() && m_terminals.empty();
}
} // namespace ns3::protection::checkbullet
