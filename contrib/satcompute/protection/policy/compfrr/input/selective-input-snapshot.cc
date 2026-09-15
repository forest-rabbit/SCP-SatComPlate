/* SPDX-License-Identifier: GPL-2.0-only */
#include "../compfrr-controller.h"

namespace ns3::protection
{
SelectiveInputSnapshot
CompFrrController::CaptureSelectiveInput(const FrequencyDecisionRecord& row, int64_t timeNs) const
{
    SelectiveInputSnapshot record;
    record.task = Task(row.taskId).definition;
    record.pair = *row.pair;
    record.trigger = row.trigger;
    record.timeNs = timeNs;
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
            record.finishExclusive = input->finishExclusive;
            record.prediction = PredictComputeFailureBeforeFinish(*input);
        }
    }
    return record;
}
} // namespace ns3::protection
