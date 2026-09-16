/* SPDX-License-Identifier: GPL-2.0-only */
#include "../compfrr-controller.h"

namespace ns3::protection
{
void
CompFrrController::PrepareSelectivePrediction(FrequencyDecisionRecord& row,
                                              uint32_t primary,
                                              int64_t remainingNs) const
{
    if (!m_optionalInput)
        return;
    row.selectiveRemainingNs = remainingNs;
    const auto input = m_faults->QueryTaskPrediction(primary, remainingNs);
    if (input)
    {
        row.selectiveFirstSampleNs = input->firstSampleTimeNs.value_or(input->predictionTimeNs);
        row.selectiveFinishExclusive = input->finishExclusive;
        row.selectivePrediction = PredictComputeFailureBeforeFinish(*input);
    }
}

PolicyAwareInputPlan
CompFrrController::EvaluateSelectiveDryRun(const FrequencyDecisionRecord& row,
                                           const TaskRuntime& task,
                                           PlacementDecision pair,
                                           DecisionPathSnapshot& paths) const
{
    PolicyAwareInputPlan plan;
    auto& record = plan.snapshot;
    record.task = task.definition;
    record.pair = pair;
    record.trigger = row.trigger;
    record.timeNs = row.input.risk.epochNs;
    record.remainingNs = row.selectiveRemainingNs;
    record.firstSampleNs = row.selectiveFirstSampleNs;
    record.finishExclusive = row.selectiveFinishExclusive;
    record.inputPath = paths.Get(record.task.sourceNodeId, pair.remoteNode);
    record.prediction = row.selectivePrediction;
    plan.decision = EvaluateSelectiveInputAdmission(
        {record.timeNs, record.remainingNs, record.firstSampleNs, record.finishExclusive,
         record.task.inputBytes, record.inputPath, record.prediction});
    plan.legacyFaultInputSeconds = record.inputPath.local || !record.task.inputBytes
        ? 0
        : row.input.inputBytes / row.input.inputBandwidth;
    plan.admissionSeconds = plan.decision.send ? 0 : plan.legacyFaultInputSeconds;
    return plan;
}

void
CompFrrController::ApplyPolicyAwareInput(FrequencyInput& input,
                                         const PolicyAwareInputPlan& plan)
{
    input.faultInputAdmissionSeconds = plan.admissionSeconds;
}

PolicyAwareInputAdmissionRecord
CompFrrController::MakePolicyAwareAudit(const FrequencyDecisionRecord& row,
                                        const PolicyAwareInputPlan& plan,
                                        const FrequencyDecision& decision,
                                        const std::string& stage,
                                        uint64_t candidateIndex)
{
    PolicyAwareInputAdmissionRecord record;
    record.taskId = row.taskId;
    record.timeNs = row.input.risk.epochNs;
    record.trigger = row.trigger;
    record.stage = stage;
    record.local = plan.snapshot.pair.localNode;
    record.remote = plan.snapshot.pair.remoteNode;
    record.candidateIndex = candidateIndex;
    record.selective = plan.decision;
    record.predictedFailureProbability = plan.snapshot.prediction
        ? plan.snapshot.prediction->predictedFailureProbability : 0;
    record.legacyFaultInputSeconds = plan.legacyFaultInputSeconds;
    record.admissionSeconds = plan.admissionSeconds;
    record.legacyDeadlineFeasible = decision.legacyFeasibleCount > 0;
    record.policyAwareDeadlineFeasible = decision.selected.has_value();
    record.rescuedByPolicyAwareInput = !record.legacyDeadlineFeasible &&
        record.policyAwareDeadlineFeasible && plan.admissionSeconds < plan.legacyFaultInputSeconds;
    record.legacyFrequencyReason = record.legacyDeadlineFeasible ? "HARD_FEASIBLE" :
        decision.legacyStorageRejected ? "STORAGE_INFEASIBLE" : "DEADLINE_INFEASIBLE";
    record.policyAwareFrequencyReason = decision.reason;
    return record;
}
} // namespace ns3::protection
