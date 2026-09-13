/* SPDX-License-Identifier: GPL-2.0-only */
#include "frequency-protection-controller.h"
#include "ns3/simulator.h"

namespace ns3::protection
{
void FrequencyProtectionController::EvaluateJit(uint64_t id, const std::string& trigger)
{
    if (m_finalized || m_manager.InputPolicy() != InputStagingPolicy::JIT) return;
    const auto& task = Task(id);
    const auto inventory = m_manager.Inventory(id);
    const auto input = m_manager.InputStaging(id);
    if (task.state != TASK_RUNNING || task.attemptGeneration || !inventory ||
        !inventory->active || !inventory->initialized || input.pendingAdmission ||
        input.failed || input.stage != InputStage::ABSENT)
    {
        m_jitWaitingCapacity.erase(id);
        return;
    }
    const auto service = Service(task.definition.computeNodeId);
    const auto running = service ? service->GetRunningTaskSnapshot() : std::nullopt;
    if (!running || running->taskId != id || running->remainingTimeNs <= 0) return;
    const auto prediction = m_faults->QueryTaskPrediction(task.definition.computeNodeId, running->remainingTimeNs);
    if (!prediction) return;
    const auto now = Simulator::Now().GetNanoSeconds();
    // A capacity or init event can coincide with a yet unprocessed check. The
    // existing post-batch observer will evaluate survivors, never this early event.
    if (prediction->firstSampleTimeNs && *prediction->firstSampleTimeNs <= now) return;
    const auto q = CombineComputeFaultProbabilities(
        prediction->f1Model ? prediction->f1State.stepFailureProbability : 0,
        prediction->f2Model ? prediction->f2State.stepFailureProbability : 0);
    const auto risk = MakeFrequencyRisk(q, *prediction);
    JitDecisionRecord row;
    row.taskId = id; row.timeNs = now; row.trigger = trigger; row.stage = input.stage;
    row.nextEvaluationNs = now + risk.intervalNs - now % risk.intervalNs;
    const auto source = task.definition.sourceNodeId, remote = inventory->config.remoteNode;
    if (!m_tasks->IsSatelliteAvailable(source) || !m_tasks->IsSatelliteAvailable(remote))
        row.admissionReason = "INPUT_ENDPOINT_UNAVAILABLE";
    else
    {
        const auto path = m_tasks->GetTransferEngine()->EstimateAdmissiblePath(source, remote);
        if (!path.admissible)
        {
            row.admissionReason = path.failureReason;
            if (path.failureReason == "NO_ADMISSIBLE_PATH") m_jitWaitingCapacity.insert(id);
        }
        else
        {
            row.inputSeconds = path.local ? 0 : task.definition.inputBytes * 8.0 / path.path.admittedRateBps;
            row.decision = EvaluateJitTiming(now, row.nextEvaluationNs, now + running->remainingTimeNs,
                                             row.inputSeconds, risk.futureSteps);
            row.admissionReason = row.decision.reason;
            if (row.decision.shouldPrefetch)
            {
                if (m_manager.Pool(remote).Free() < task.definition.inputBytes)
                    row.admissionReason = "INPUT_STORAGE_UNAVAILABLE";
                else
                {
                    row.admitted = m_manager.TryStartInputPrefetch(id);
                    row.admissionReason = row.admitted ? "INPUT_REQUEST_ACCEPTED" : "INPUT_NOT_ADMITTED";
                    const auto staged = m_manager.InputStaging(id);
                    row.objectId = staged.objectId; row.transferId = staged.transferId;
                }
                if (!row.admitted) m_jitWaitingCapacity.insert(id);
            }
            else m_jitWaitingCapacity.erase(id);
        }
    }
    if (row.admitted) m_jitWaitingCapacity.erase(id);
    m_jitDecisions.push_back(std::move(row));
}
} // namespace ns3::protection
