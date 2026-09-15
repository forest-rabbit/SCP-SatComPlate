/* SPDX-License-Identifier: GPL-2.0-only */
#include "input-staging-manager.h"
#include "../../../traffic/local-delivery.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
namespace { int64_t Now() { return Simulator::Now().GetNanoSeconds(); } }
const char* ToString(OptionalInputState state)
{
    switch (state)
    {
    case OptionalInputState::ABSENT: return "ABSENT";
    case OptionalInputState::REQUESTED: return "REQUESTED";
    case OptionalInputState::IN_FLIGHT: return "IN_FLIGHT";
    case OptionalInputState::READY: return "READY";
    case OptionalInputState::FAILED: return "FAILED";
    case OptionalInputState::RELEASED: return "RELEASED";
    }
    throw std::logic_error("invalid optional INPUT state");
}
InputStagingManager::InputStagingManager(Ptr<TaskCoordinator> tasks, CheckpointRecoveryPort& manager,
    int64_t stop, std::function<uint64_t(uint32_t)> free)
    : m_tasks(tasks), m_network(tasks->GetTransferEngine()), m_manager(manager), m_stopNs(stop),
      m_free(std::move(free)) {}
InputStagingManager::~InputStagingManager()
{
    for (auto& [id, r] : m_records) Simulator::Cancel(r.localEvent);
}
void InputStagingManager::Log(Record& r, const std::string& event)
{
    m_events.push_back({r.task.taskId, r.flow, r.object, r.flow ? m_network->GetSentBytes(r.flow) : 0,
                        r.target, Now(), event, ToString(r.state), r.reason});
}
void InputStagingManager::ObserveStorage()
{
    std::map<uint32_t, Peak> nodes;
    for (const auto& [id, r] : m_records)
        if (const auto object = r.object ? m_manager.Pool(r.target).Find(r.object) : nullptr)
            (object->reserved ? nodes[r.target].reserved : nodes[r.target].used) += object->bytes;
    Peak total;
    for (auto& [node, current] : nodes)
    {
        current.total = current.used + current.reserved;
        auto& p = m_nodePeaks[node];
        p.used = std::max(p.used, current.used); p.reserved = std::max(p.reserved, current.reserved);
        p.total = std::max(p.total, current.total);
        total.used += current.used; total.reserved += current.reserved;
    }
    m_peak.used = std::max(m_peak.used, total.used); m_peak.reserved = std::max(m_peak.reserved, total.reserved);
    m_peak.total = std::max(m_peak.total, total.used + total.reserved);
}
void InputStagingManager::ReleaseObject(Record& r)
{
    if (r.object)
    {
        auto& pool = m_manager.Pool(r.target);
        pool.ReleaseReservation(r.object); pool.Release(r.object);
        ObserveStorage();
    }
}
void InputStagingManager::Request(const TaskDefinition& task, uint32_t target)
{
    auto [it, inserted] = m_records.try_emplace(task.taskId);
    if (!inserted) throw std::logic_error("second proactive INPUT attempt forbidden");
    auto& r = it->second; r.task = task; r.target = target; r.requestedNs = Now();
    if (Now() >= m_stopNs - 1 || !m_tasks->IsSatelliteAvailable(task.sourceNodeId) ||
        !m_tasks->IsSatelliteAvailable(target)) return End(r, "ENDPOINT_UNAVAILABLE");
    if ((m_free && m_free(target) < task.inputBytes) || m_manager.Pool(target).Free() < task.inputBytes)
        return End(r, "STORAGE_NOT_ADMITTED");
    const auto path = m_network->EstimateAdmissiblePath(task.sourceNodeId, target);
    if (!path.TransferTimeNs(task.inputBytes)) return End(r, "PATH_NOT_ADMITTED");
    const auto object = m_manager.Pool(target).TryReserve(task.taskId, StorageKind::INPUT_STAGING, task.inputBytes);
    if (!object) return End(r, "STORAGE_NOT_ADMITTED");
    r.object = *object; r.state = OptionalInputState::REQUESTED; ObserveStorage(); Log(r, "PREFETCH_REQUESTED");
    if (task.sourceNodeId == target)
    {
        r.localEvent = LocalDelivery::Schedule(task.sourceNodeId, target, task.inputBytes,
            [this, &r] { return !r.closed && m_tasks->IsSatelliteAvailable(r.target); },
            [this, &r](uint64_t, int64_t) { Ready(r); });
        // Logical delivery has no sender, no UDP and zero network bytes.
        r.startedNs = Now(); Log(r, "PREFETCH_LOCAL_STARTED");
        return;
    }
    m_manager.Transfers().Queue(Now(), {{task.taskId, 0, ProtectionTransferKind::PREFETCH_INPUT, 0},
        task.sourceNodeId, target, task.inputBytes, 0, r.object, Now(),
        [this, &r] {
            if (r.closed) return false;
            if (!m_tasks->IsSatelliteAvailable(r.task.sourceNodeId) || !m_tasks->IsSatelliteAvailable(r.target) ||
                !m_network->EstimateAdmissiblePath(r.task.sourceNodeId, r.target).TransferTimeNs(r.task.inputBytes))
            { End(r, "PATH_NOT_ADMITTED"); return false; }
            return true;
        },
        [this, &r](uint64_t flow) {
            if (!flow) return End(r, "REGISTRATION_NOT_ADMITTED");
            r.flow = flow; m_flows.emplace(flow, r.task.taskId);
            m_network->SetTerminalObserver(flow, MakeCallback(&InputStagingManager::Terminal, this));
            m_network->SetOptionalFirstAdmission(flow, [this, id=r.task.taskId] { Started(id); });
        }}, "duplicate optional INPUT request");
}
void InputStagingManager::Started(uint64_t task)
{
    auto& r = m_records.at(task);
    if (r.closed) throw std::logic_error("closed INPUT sender started");
    r.startedNs = Now(); r.state = OptionalInputState::IN_FLIGHT; Log(r, "PREFETCH_STARTED");
}
void InputStagingManager::Ready(Record& r)
{
    if (r.closed) return;
    if (!m_tasks->IsSatelliteAvailable(r.target) || !m_manager.Pool(r.target).CommitReservation(r.object))
        return End(r, "HOLDER_UNAVAILABLE", true);
    r.readyNs = Now(); r.state = OptionalInputState::READY; ObserveStorage(); Log(r, "PREFETCH_READY");
    if (r.consumer) { auto callback = std::move(r.consumer); callback(true, r.flow); }
}
void InputStagingManager::Terminal(uint64_t flow, int64_t)
{
    auto& r = m_records.at(m_flows.at(flow));
    if (r.closed) return;
    if (m_network->IsCompleted(flow)) Ready(r);
    else End(r, r.startedNs < 0 ? "PATH_NOT_ADMITTED" : "PREFETCH_TRANSFER_FAILED", r.startedNs >= 0);
}
void InputStagingManager::End(Record& r, const std::string& reason, bool failed)
{
    if (r.closed) return;
    r.closed = true; r.reason = reason; r.failed |= failed; r.releasedNs = Now();
    auto consumer = std::move(r.consumer);
    Simulator::Cancel(r.localEvent);
    if (r.flow && !m_network->IsTerminal(r.flow))
        m_network->FinalizeTransferIfActive(r.flow,
            failed ? TransferTerminalState::FAILED : TransferTerminalState::CANCELLED,
            reason == "SOURCE_F3" ? TransferTerminalReason::SOURCE_SATELLITE_FAILED :
            reason == "HOLDER_F3" ? TransferTerminalReason::DESTINATION_SATELLITE_FAILED :
            TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
    ReleaseObject(r);
    r.state = r.startedNs < 0 ? OptionalInputState::ABSENT : failed ? OptionalInputState::FAILED : OptionalInputState::RELEASED;
    Log(r, r.startedNs < 0 ? "PREFETCH_NOT_ADMITTED" : failed ? "PREFETCH_FAILED" : "PREFETCH_RELEASED");
    if (consumer) consumer(false, r.flow);
}
InputDependency InputStagingManager::Resolve(const TaskDefinition& task, uint32_t target) const
{
    InputDependency result; result.target = target;
    const auto it = m_records.find(task.taskId);
    if (it != m_records.end())
    {
        const auto& r = it->second;
        const auto object = r.object ? m_manager.Pool(r.target).Find(r.object) : nullptr;
        if (!r.closed && r.target == target && object && m_tasks->IsSatelliteAvailable(target))
        {
            result.objectId = r.object; result.flowId = r.flow;
            if (r.state == OptionalInputState::READY && !object->reserved)
            { result.mode = InputDependencyMode::READY; result.remainingNs = 0; result.readyNs = r.readyNs; return result; }
            // A registered ID is not actual first admission. Only local delivery may
            // have an existing pending dependency without an admitted network sender.
            const bool pendingLocal = r.state == OptionalInputState::REQUESTED &&
                                      r.task.sourceNodeId == r.target;
            if (((r.state == OptionalInputState::IN_FLIGHT && r.flow) || pendingLocal) &&
                m_tasks->IsSatelliteAvailable(task.sourceNodeId))
            {
                result.mode = InputDependencyMode::IN_FLIGHT;
                result.remainingNs = r.flow ? m_network->EstimateRemainingReceiverTimeNs(r.flow)
                                           : std::optional<int64_t>{1};
                return result;
            }
        }
        if (!r.closed && r.state == OptionalInputState::REQUESTED && r.task.sourceNodeId != r.target)
            result.diagnostic = "PREFETCH_NOT_ESTABLISHED";
        if (r.startedNs >= 0)
            result.refetchReason = r.failed ? "FAILED_PREFETCH_REFETCH" :
                r.target != target ? "WRONG_TARGET_REFETCH" : "FAILED_PREFETCH_REFETCH";
    }
    if (m_tasks->IsSatelliteAvailable(task.sourceNodeId) && m_tasks->IsSatelliteAvailable(target))
        result.remainingNs = m_network->EstimateAdmissiblePath(task.sourceNodeId, target).TransferTimeNs(task.inputBytes);
    return result;
}
void InputStagingManager::Accept(uint64_t task, const InputDependency& dependency,
                                 std::function<void(bool, uint64_t)> completed)
{
    const auto it = m_records.find(task);
    if (it == m_records.end()) return;
    auto& r = it->second;
    if (dependency.mode == InputDependencyMode::FETCH)
    {
        r.refetchReason = dependency.refetchReason;
        r.wrongTarget = dependency.refetchReason == "WRONG_TARGET_REFETCH";
        if (!r.refetchReason.empty()) Log(r, r.refetchReason);
        End(r, r.wrongTarget ? "WRONG_TARGET_REFETCH" :
               dependency.diagnostic.empty() ? "RECOVERY_REFETCH" : dependency.diagnostic, r.failed);
        return;
    }
    const auto current = Resolve(r.task, dependency.target);
    if (current.mode != dependency.mode || current.objectId != dependency.objectId ||
        current.flowId != dependency.flowId || !current.remainingNs || r.handedOff)
        throw std::logic_error("accepted INPUT dependency changed or was adopted twice");
    r.handedOff = true; Log(r, "PREFETCH_HANDOFF");
    if (current.mode == InputDependencyMode::READY) completed(true, r.flow);
    else r.consumer = std::move(completed);
}
void InputStagingManager::Freeze(uint64_t task)
{
    const auto it = m_records.find(task);
    if (it == m_records.end() || it->second.faultNs >= 0) return;
    auto& r = it->second; r.faultNs = Now(); r.sentAtFault = r.flow ? m_network->GetSentBytes(r.flow) : 0;
    Log(r, "PREFETCH_FAULT_SNAPSHOT");
}
void InputStagingManager::Fault(uint64_t task, const TaskFaultNodeChange& change)
{
    const auto it = m_records.find(task);
    if (it == m_records.end() || change.kind != TaskFaultKind::SATELLITE) return;
    auto& r = it->second;
    if (r.target == change.nodeId || (r.task.sourceNodeId == change.nodeId && r.readyNs < 0))
        End(r, r.target == change.nodeId ? "HOLDER_F3" : "SOURCE_F3", true);
}
void InputStagingManager::ComputeStarted(uint64_t task, uint32_t target)
{
    const auto it = m_records.find(task);
    if (it == m_records.end()) return;
    auto& r = it->second;
    if (r.handedOff && !r.closed && target == r.target && r.state == OptionalInputState::READY)
    {
        if (r.usedNs >= 0) throw std::logic_error("INPUT consumed twice");
        r.usedNs = Now(); Log(r, "PREFETCH_USED"); End(r, "RECOVERY_COMPUTE_STARTED");
    }
}
void InputStagingManager::Release(uint64_t task)
{
    const auto it = m_records.find(task);
    if (it != m_records.end()) End(it->second, "TASK_TERMINAL");
}
void InputStagingManager::Finalize()
{
    for (auto& [id, r] : m_records) End(r, "SIMULATION_ENDED");
}
} // namespace ns3::protection
