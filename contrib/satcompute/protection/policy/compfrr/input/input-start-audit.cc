/* SPDX-License-Identifier: GPL-2.0-only */
#include "../compfrr-controller.h"

namespace ns3::protection
{
InputStartAuditRecord
CompFrrController::CaptureInputStart(const FrequencyDecisionRecord& row, int64_t timeNs) const
{
    InputStartAuditRecord record;
    record.task = Task(row.taskId).definition;
    record.pair = *row.pair;
    record.config = row.proposal.selected->config;
    record.trigger = row.trigger;
    record.timeNs = timeNs;
    record.deadlineNs = Task(row.taskId).computeDeadlineTimeNs;
    record.progressWork = row.progressWork;
    record.primaryRate = Service(record.task.computeNodeId)->GetComputeRateWorkUnitsPerSecond();
    record.recoveryRate = Service(record.pair.remoteNode)->GetComputeRateWorkUnitsPerSecond();
    record.localFreeBytes = m_manager.Pools().at(record.pair.localNode)->Free();
    record.remoteFreeBytes = m_manager.Pools().at(record.pair.remoteNode)->Free();
    record.sourceAvailable = m_tasks->IsSatelliteAvailable(record.task.sourceNodeId);
    record.remoteAvailable = m_tasks->IsSatelliteAvailable(record.pair.remoteNode);
    record.inputPath = m_tasks->GetTransferEngine()->EstimateAdmissiblePath(record.task.sourceNodeId,
                                                                         record.pair.remoteNode);
    const auto live = Service(record.task.computeNodeId)->GetRunningTaskSnapshot();
    if (live && live->taskId == row.taskId)
    {
        record.remainingNs = live->remainingTimeNs;
        const auto input = m_faults->QueryTaskPrediction(record.task.computeNodeId, live->remainingTimeNs);
        if (input)
        {
            record.firstSampleNs = input->firstSampleTimeNs.value_or(input->predictionTimeNs);
            record.intervalNs = input->checkIntervalNs;
            record.finishExclusive = input->finishExclusive;
            record.prediction = PredictComputeFailureBeforeFinish(*input);
        }
    }
    // Existing deterministic task adapter, not a future checkpoint simulator.
    const TaskStateAdapter layout(record.task);
    record.headerBytes = layout.HeaderBytes();
    record.legalWork = layout.Floor(row.progressWork);
    record.initialVariableStateBytes = layout.StateBytes(record.legalWork);
    record.referenceInput = row.input;
    record.referenceInput.storageDemand = {};
    record.referenceProposal = row.proposal;
    if (m_n5c && row.n5cTrace) record.referencePair = m_n5c->Decision(*row.n5cTrace).reference;
    record.actualValidation = row.inputAuditValidation;
    return record;
}
} // namespace ns3::protection
