/* SPDX-License-Identifier: GPL-2.0-only */
#include "checkpoint-manager.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
const char* InputStageName(InputStage stage)
{
    switch (stage)
    {
    case InputStage::ABSENT: return "ABSENT";
    case InputStage::IN_FLIGHT: return "IN_FLIGHT";
    case InputStage::READY: return "READY";
    case InputStage::RELEASED: return "RELEASED";
    }
    throw std::logic_error("unknown INPUT stage");
}

InputStagingSnapshot CheckpointManager::InputStaging(uint64_t id) const
{
    const auto found = m_inputs.find(id);
    if (found == m_inputs.end()) return {};
    auto out = found->second.snapshot;
    if (out.transferId) out.totalSentBytes = m_network->GetSentBytes(out.transferId);
    return out;
}

std::vector<InputStagingSnapshot> CheckpointManager::InputStagingHistory() const
{
    std::vector<InputStagingSnapshot> result;
    for (const auto& [id, input] : m_inputs) result.push_back(InputStaging(id));
    return result;
}

bool CheckpointManager::InputLive(uint64_t id) const
{
    const auto& s = m_inputs.at(id).snapshot;
    const auto& state = *m_states.at(id);
    const auto& task = state.task;
    if (s.failed || s.stage == InputStage::RELEASED ||
        Simulator::Now().GetNanoSeconds() >= m_stopNs ||
        !m_tasks->IsSatelliteAvailable(s.holderNode) ||
        !m_tasks->IsSatelliteAvailable(s.sourceNode)) return false;
    // Compute F1/F2 on a holder does not destroy its storage/network role.
    return s.heldForRecovery ? !IsTerminalTaskState(task.state)
        : state.active && state.initialized && task.state == TASK_RUNNING && !task.attemptGeneration;
}

bool CheckpointManager::TryStartInputPrefetch(uint64_t id)
{
    if (m_inputPolicy != InputStagingPolicy::JIT) return false;
    const auto found = m_states.find(id);
    if (found == m_states.end() || !found->second->active || !found->second->initialized) return false;
    auto& state = *found->second;
    auto& input = m_inputs[id];
    auto& s = input.snapshot;
    if (s.pendingAdmission || s.stage != InputStage::ABSENT || s.failed) return false;
    s.taskId = id; s.bytes = state.task.definition.inputBytes;
    s.sourceNode = state.task.definition.sourceNodeId; s.holderNode = state.config.remoteNode;
    s.local = s.sourceNode == s.holderNode;
    if (!InputLive(id)) return false;
    // Preview is a new-flow admission check only here, never on an existing stream.
    const auto path = m_network->EstimateAdmissiblePath(s.sourceNode, s.holderNode);
    if (!path.admissible) return false;
    const auto object = Reserve(state, s.holderNode, StorageKind::INPUT_STAGING, s.bytes, 0);
    if (!object) return false;
    s.objectId = *object; s.pendingAdmission = true;
    s.requestedNs = Simulator::Now().GetNanoSeconds();
    Log(state, "PREFETCH_INPUT_REQUESTED", 0, s.bytes, s.holderNode, s.objectId);
    if (s.local)
    {
        s.pendingAdmission = false; s.stage = InputStage::IN_FLIGHT;
        s.registeredNs = s.requestedNs;
        input.localEvent = LocalDelivery::Schedule(s.sourceNode, s.holderNode, s.bytes,
            [this, id] { return InputLive(id); },
            [this, id](auto, auto at) { InputReceived(id, at); });
        Log(state, "PREFETCH_INPUT_LOCAL_STARTED", 0, s.bytes, s.holderNode, s.objectId);
        return true;
    }
    QueueOwnedTransfer({id, 0, ProtectionTransferKind::PREFETCH_INPUT, 0},
        s.sourceNode, s.holderNode, s.bytes, 0, s.objectId,
        [this, id] {
            if (InputLive(id)) return true;
            ReleaseInput(id, "NOT_ADMITTED_TASK_UNAVAILABLE");
            return false;
        },
        [this, id](uint64_t transfer) {
            auto& s = m_inputs.at(id).snapshot;
            s.pendingAdmission = false;
            if (!transfer)
            {
                Pool(s.holderNode).ReleaseReservation(s.objectId);
                Log(*m_states.at(id), "PREFETCH_INPUT_NOT_ADMITTED", 0, s.bytes, s.holderNode, s.objectId);
                s.objectId = 0;
                // No flow was established: retain ABSENT and eligibility for a legal retry.
                return;
            }
            s.transferId = transfer; s.stage = InputStage::IN_FLIGHT;
            s.registeredNs = Simulator::Now().GetNanoSeconds();
            m_network->SetTerminalObserver(transfer, MakeCallback(&CheckpointManager::InputTerminal, this));
            Log(*m_states.at(id), "PREFETCH_INPUT_STARTED", 0, s.bytes, s.holderNode, s.objectId, transfer);
        });
    return true;
}

void CheckpointManager::InputReceived(uint64_t id, int64_t at)
{
    auto& s = m_inputs.at(id).snapshot;
    if (s.stage != InputStage::IN_FLIGHT) return;
    if (!m_tasks->IsSatelliteAvailable(s.holderNode))
        return ReleaseInput(id, "PREFETCH_HOLDER_UNAVAILABLE");
    if (!Pool(s.holderNode).CommitReservation(s.objectId))
        throw std::logic_error("prefetch receiver lost independent S reservation");
    s.stage = InputStage::READY; s.readyNs = at;
    Log(*m_states.at(id), "PREFETCH_INPUT_READY", 0, s.bytes, s.holderNode, s.objectId, s.transferId);
    const auto terminal = std::move(m_inputs.at(id).terminal);
    if (terminal) terminal(true);
}

void CheckpointManager::InputTerminal(uint64_t transfer, int64_t at)
{
    const auto flow = std::find_if(m_flows.begin(), m_flows.end(),
                                  [transfer](const auto& f) { return f.transferId == transfer; });
    if (flow == m_flows.end()) throw std::logic_error("unknown prefetch transfer");
    auto& input = m_inputs.at(flow->key.taskId);
    auto& s = input.snapshot;
    s.totalSentBytes = m_network->GetSentBytes(transfer);
    if (s.stage == InputStage::RELEASED) return;
    if (m_network->IsCompleted(transfer)) InputReceived(s.taskId, at);
    else
    {
        s.failed = true;
        ReleaseInput(s.taskId, "PREFETCH_TRANSFER_FAILED");
    }
}

InputDependency CheckpointManager::ResolveInputDependency(uint64_t id, uint32_t target) const
{
    InputDependency out;
    out.holderNode = target;
    const auto found = m_inputs.find(id);
    if (found == m_inputs.end()) return out;
    const auto& s = found->second.snapshot;
    if (s.holderNode != target || s.failed || !m_tasks->IsSatelliteAvailable(target)) return out;
    const auto entry = m_pools.at(target)->Find(s.objectId);
    if (!entry || entry->taskId != id || entry->bytes != s.bytes || entry->kind != StorageKind::INPUT_STAGING)
        return out;
    out.stage = s.stage; out.objectId = s.objectId; out.transferId = s.transferId; out.local = s.local;
    if (s.stage == InputStage::READY && !entry->reserved)
    {
        out.reusable = true; out.requiresSource = false; out.waitNs = 0;
    }
    else if (s.stage == InputStage::IN_FLIGHT && m_tasks->IsSatelliteAvailable(s.sourceNode))
    {
        out.reusable = s.local || (s.transferId && !m_network->IsTerminal(s.transferId));
        if (out.reusable)
            out.waitNs = s.local ? std::optional<int64_t>{1}
                                 : m_network->EstimateRemainingTransferTimeNs(s.transferId);
    }
    return out;
}

void CheckpointManager::HoldInputForRecovery(uint64_t id, int64_t at)
{
    const auto found = m_inputs.find(id);
    if (found == m_inputs.end()) return;
    auto& s = found->second.snapshot;
    if (s.faultNs >= 0) return;
    s.faultNs = at;
    s.sentBeforeFaultBytes = s.transferId ? m_network->GetSentBytes(s.transferId) : 0;
    s.heldForRecovery = s.stage == InputStage::READY || s.stage == InputStage::IN_FLIGHT;
    // A request awaiting registration is not a flow to preserve across this fault.
    if (s.pendingAdmission) ReleaseInput(id, "FAULT_BEFORE_PREFETCH_ADMISSION");
}

bool CheckpointManager::AdoptInput(uint64_t id, uint32_t target, std::function<void(bool)> terminal)
{
    const auto dependency = ResolveInputDependency(id, target);
    if (!dependency.reusable) return false;
    auto& input = m_inputs.at(id);
    auto& s = input.snapshot;
    s.heldForRecovery = s.adopted = true;
    Log(*m_states.at(id), "PREFETCH_INPUT_HANDOFF", 0, s.bytes, target, s.objectId, s.transferId);
    // READY is consumed by the caller without a new delivery. IN_FLIGHT keeps its
    // original terminal observer/flow ID; only the dependency subscriber changes.
    if (dependency.stage == InputStage::IN_FLIGHT) input.terminal = std::move(terminal);
    return true;
}

void CheckpointManager::MarkInputUsed(uint64_t id)
{
    const auto found = m_inputs.find(id);
    if (found == m_inputs.end() || !found->second.snapshot.adopted) return;
    auto& s = found->second.snapshot;
    if (s.stage != InputStage::READY) throw std::logic_error("compute consumed incomplete INPUT");
    s.used = true;
    Log(*m_states.at(id), "PREFETCH_INPUT_USED", 0, s.bytes, s.holderNode, s.objectId, s.transferId);
}

void CheckpointManager::ReleaseInput(uint64_t id, const std::string& reason)
{
    const auto found = m_inputs.find(id);
    if (found == m_inputs.end()) return;
    auto& input = found->second;
    auto& s = input.snapshot;
    if (s.stage == InputStage::RELEASED) return;
    s.stage = InputStage::RELEASED; s.pendingAdmission = false;
    s.releasedNs = Simulator::Now().GetNanoSeconds(); s.terminalReason = reason;
    Simulator::Cancel(input.localEvent);
    if (s.transferId)
    {
        m_network->FinalizeTransferIfActive(s.transferId, TransferTerminalState::CANCELLED,
                                          TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
        s.totalSentBytes = m_network->GetSentBytes(s.transferId);
    }
    if (s.objectId)
    {
        Pool(s.holderNode).ReleaseReservation(s.objectId);
        Pool(s.holderNode).Release(s.objectId);
    }
    Log(*m_states.at(id), "PREFETCH_INPUT_RELEASED", 0, s.bytes, s.holderNode, s.objectId, s.transferId);
    const auto terminal = std::move(input.terminal);
    if (terminal && !s.used) terminal(false);
}

void CheckpointManager::InputSatelliteFault(uint64_t id, uint32_t node)
{
    const auto found = m_inputs.find(id);
    if (found == m_inputs.end()) return;
    const auto& s = found->second.snapshot;
    if (s.stage == InputStage::RELEASED) return;
    // Fully received strictly before the same-ns batch has no source dependency.
    if (s.holderNode == node || (s.sourceNode == node &&
        (s.stage != InputStage::READY || s.readyNs >= Simulator::Now().GetNanoSeconds())))
        ReleaseInput(id, "PREFETCH_F3_DEPENDENCY_LOST");
}
} // namespace ns3::protection
