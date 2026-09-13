/* SPDX-License-Identifier: GPL-2.0-only */
#include "recovery-controller.h"
#include "ns3/simulator.h"
#include <stdexcept>

namespace ns3::protection
{
InputDependency RecoveryController::ResolveInput(const State& state, uint32_t target, bool fromZero) const
{
    if (m_manager.InputPolicy() == InputStagingPolicy::JIT)
    {
        const auto input = m_manager.ResolveInputDependency(state.task.definition.taskId, target);
        if (input.reusable) return input;
    }
    InputDependency input;
    input.holderNode = target;
    input.local = state.task.definition.sourceNodeId == target;
    if (Deferred() || fromZero)
        input.waitNs = Estimate(state.task.definition.sourceNodeId, target, state.task.definition.inputBytes);
    else
    {
        // Eager checkpoint contains INPUT; no independent staging object exists.
        input.stage = InputStage::READY; input.requiresSource = false; input.waitNs = 0;
    }
    return input;
}

bool RecoveryController::InputRequiresSource(const State& state) const
{
    if (state.summary.inputReceivedNs >= 0) return false;
    if (m_manager.InputPolicy() == InputStagingPolicy::JIT)
    {
        // During a same-ns batch BeginRecovery may already have run, but target
        // acceptance is deliberately at +1ns. Preserve a READY holder through
        // this boundary; final candidate checks decide whether it can be used.
        const auto target = state.summary.recoveryNode.value_or(state.summary.snapshot.remoteNode);
        const auto input = m_manager.ResolveInputDependency(state.task.definition.taskId,
                                                            target);
        if (input.reusable && !input.requiresSource) return false;
    }
    return Deferred() || state.summary.path == "RECOMPUTE";
}

void RecoveryController::StartInput(State& state, const InputDependency& input)
{
    auto& r = state.summary;
    const auto id = state.task.definition.taskId;
    if (m_manager.InputPolicy() == InputStagingPolicy::JIT && input.reusable)
    {
        if (!m_manager.AdoptInput(id, *r.recoveryNode, [this, &state, transfer = input.transferId](bool ready) {
                if (!state.live) return;
                if (ready) Received(state, ProtectionTransferKind::RECOVERY_INPUT,
                                    state.task.definition.inputBytes, transfer);
                else Fail(state, "REUSED_INPUT_TRANSFER_FAILED");
            })) throw std::logic_error("INPUT changed between same-event preview and adoption");
        r.inputReused = true;
        r.inputStateAtAcceptance = InputStageName(input.stage);
        r.reusedInputTransferId = input.transferId; r.reusedInputObjectId = input.objectId;
        r.inputMode = input.local ? "LOCAL" : "NETWORK";
        r.inputStartedNs = Simulator::Now().GetNanoSeconds();
        if (input.transferId) state.transfers.push_back(input.transferId);
        Log(state, "RECOVERY_INPUT_HANDOFF", state.task.definition.inputBytes, input.transferId, r.inputMode);
        if (input.stage == InputStage::READY)
        {
            r.inputReceivedNs = r.inputStartedNs; // Zero recovery wait, historical arrival remains in INPUT ledger.
            Log(state, "RECOVERY_INPUT_REUSED_READY", state.task.definition.inputBytes,
                input.transferId, r.inputMode);
        }
        return;
    }
    if (m_manager.InputPolicy() == InputStagingPolicy::JIT)
    {
        r.inputStateAtAcceptance = "ABSENT";
        m_manager.ReleaseInput(id, "RECOVERY_TARGET_CHANGED_OR_INPUT_UNAVAILABLE");
    }
    Deliver(state, ProtectionTransferKind::RECOVERY_INPUT,
            state.task.definition.sourceNodeId, *r.recoveryNode, state.task.definition.inputBytes);
}
} // namespace ns3::protection
