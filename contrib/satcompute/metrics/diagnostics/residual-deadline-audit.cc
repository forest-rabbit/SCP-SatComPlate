/* SPDX-License-Identifier: GPL-2.0-only */
#include "residual-deadline-audit.h"
#include "../../protection/policy/compfrr/compfrr-controller.h"
#include "../../protection/policy/compfrr/input/input-cost-adapter.h"
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <tuple>

namespace ns3::protection
{
ResidualRecoveryMinimum InspectMinimumRecovery(const FrequencyInput& in)
{
    ResidualRecoveryMinimum out;
    if (!in.nodeAvailable || !in.pathAvailable || !in.replayAvailable || !in.storageDemand)
        return out;
    // This is a diagnostic minimum, NOT a replacement objective or START solver.
    // Preserve the production configuration space, but do not filter on deadline.
    for (uint32_t d = 10; d <= 100; ++d)
        for (uint32_t n = 1; n <= 100 && n * d <= 1000; ++n)
        {
            const FrequencyConfiguration config{d, n};
            const auto demand = in.storageDemand(config);
            if (!demand || demand->localAdditionalBytes > in.localFreeBytes ||
                demand->remoteAdditionalBytes > in.remoteFreeBytes)
            {
                ++out.storageRejected;
                continue;
            }
            const auto seconds = FrequencyRecoverySeconds(in, config);
            if (!std::isfinite(seconds) || seconds < 0)
                throw std::invalid_argument("invalid residual model recovery cost");
            ++out.resourceFeasible;
            if (!out.config || std::tuple{seconds, d, n} <
                std::tuple{*out.seconds, out.config->deltaPermille, out.config->batchN})
            {
                out.config = config;
                out.seconds = seconds;
                out.storage = *demand;
            }
        }
    return out;
}

void CompFrrController::RecordResidualDeadlineAudit(const FrequencyDecisionRecord& row)
{
    ResidualDeadlineAuditRecord record;
    record.decisionIndex = m_decisions.size();
    record.candidateIndex = row.candidateCoverage->checked;
    record.candidateCount = row.candidateCoverage->candidates;
    record.input = row.input;
    record.minimum = InspectMinimumRecovery(row.input);
    record.input.storageDemand = {}; // Never retain a callback or live inventory owner.
    record.slackSeconds = row.proposal.deadlineSlackSeconds;
    record.initializationSeconds = row.proposal.initializationSeconds;
    record.originalReason = row.proposal.reason;
    // Reuse causal QueryTaskPrediction/path semantics, but this pair is hypothetical.
    // Do not call the real admission/staging manager or move the production snapshot.
    record.selective = CaptureSelectiveInput(row, row.input.risk.epochNs);
    const auto& a = record.selective;
    record.selector = EvaluateSelectiveInputAdmission(
        {a.timeNs, a.remainingNs, a.firstSampleNs, a.finishExclusive,
         a.task.inputBytes, a.inputPath, a.prediction});
    m_residualAudit.push_back(std::move(record));
}

void CompFrrController::WriteResidualDeadlineAudit(const std::filesystem::path& directory) const
{
    if (m_residualAuditTasks.empty()) return;
    using Json = nlohmann::json;
    Json records = Json::array();
    for (const auto& r : m_residualAudit)
    {
        const auto& a = r.selective;
        const auto& d = m_decisions.at(r.decisionIndex);
        if (d.taskId != a.task.taskId || d.input.risk.epochNs != a.timeNs || d.trigger != a.trigger)
            throw std::logic_error("residual audit decision identity mismatch");
        const auto& in = r.input;
        const auto& path = a.inputPath;
        const auto originalTi = InputCostAdapter(in.inputPolicy).FaultInputSeconds(
            in.replayAvailable, in.inputBytes, in.inputBandwidth);
        const double ti = path.local ? 0 : originalTi;
        Json steps = Json::array(), frequencySteps = Json::array(), hops = Json::array();
        if (a.prediction)
            for (const auto& step : a.prediction->steps)
                steps.push_back({{"time_ns", step.targetTimeNs}, {"q_f1", step.f1StepFailureProbability},
                    {"q_f2", step.f2StepFailureProbability}, {"q_comp", step.combinedStepFailureProbability}});
        for (const auto& step : in.risk.futureSteps)
            frequencySteps.push_back({{"time_ns", step.targetTimeNs}, {"q_comp", step.combinedStepFailureProbability}});
        for (const auto& hop : path.path.hops)
            hops.push_back({hop.sourceSatelliteId, hop.destinationSatelliteId});
        records.push_back({
            {"task_id", a.task.taskId}, {"profile", TaskProfileToString(a.task.taskProfile)},
            {"time_ns", a.timeNs}, {"trigger", a.trigger}, {"source", a.task.sourceNodeId},
            {"primary", a.task.computeNodeId}, {"fixed_local", a.pair.localNode}, {"remote", a.pair.remoteNode},
            {"candidate_index", r.candidateIndex}, {"candidate_count", r.candidateCount},
            {"input_bytes", a.task.inputBytes}, {"work", in.work}, {"variable_bytes", in.variableBytes},
            {"progress", in.progress}, {"progress_work", d.progressWork}, {"deadline_ns", in.deadlineNs},
            {"slack_seconds", r.slackSeconds}, {"fault_input_seconds", ti},
            {"original_frequency_fault_input_seconds", originalTi},
            {"recovery_no_full_input_min_seconds", r.minimum.seconds ? Json(*r.minimum.seconds) : Json(nullptr)},
            {"delta_permille", r.minimum.config ? Json(r.minimum.config->deltaPermille) : Json(nullptr)},
            {"batch_n", r.minimum.config ? Json(r.minimum.config->batchN) : Json(nullptr)},
            {"resource_feasible_configs", r.minimum.resourceFeasible}, {"storage_rejected_configs", r.minimum.storageRejected},
            {"local_free_bytes", in.localFreeBytes}, {"remote_free_bytes", in.remoteFreeBytes},
            {"best_local_additional_bytes", r.minimum.storage.localAdditionalBytes},
            {"best_remote_additional_bytes", r.minimum.storage.remoteAdditionalBytes},
            {"recovery_rate", in.recoveryRate}, {"input_bandwidth_bytes_per_s", in.inputBandwidth},
            {"backup_bandwidth_bytes_per_s", in.backupBandwidth}, {"cL_ns", in.costs.localNs}, {"cR_ns", in.costs.remoteNs},
            {"initialization_seconds", r.initializationSeconds}, {"remaining_ns", a.remainingNs},
            {"original_reason", r.originalReason}, {"node_available", in.nodeAvailable}, {"path_available", in.pathAvailable},
            {"input_path", {{"reachable", path.reachable}, {"admissible", path.admissible}, {"local", path.local},
                {"rate_bps", path.path.admittedRateBps}, {"propagation_ns", path.propagationNs},
                {"hops", hops}, {"reason", path.failureReason}}},
            {"frequency_first_sample_ns", d.firstSampleNs}, {"frequency_steps", frequencySteps},
            {"frequency_finish_exclusive", a.trigger != "FAULT_EPOCH"},
            {"frequency_p_fail", in.risk.pFailBeforeFinish},
            {"selective_first_sample_ns", a.firstSampleNs}, {"selective_finish_exclusive", a.finishExclusive},
            {"selective_p_fail", a.prediction ? Json(a.prediction->predictedFailureProbability) : Json(nullptr)},
            {"selective_steps", steps}, {"selective_pure_send", r.selector.send},
            {"selective_reason", r.selector.reason}, {"serialization_ns", r.selector.serializationNs},
            {"network_ready_ns", r.selector.networkReadyNs}, {"serial_gain_ns", r.selector.serialGainNs},
            {"serial_cost_ns", r.selector.costNs}, {"observed_fault_hit", d.faultHit},
            {"observed_committed", d.committed}, {"observed_resolution", d.reason}
        });
    }
    std::ofstream out(directory / "residual-deadline-candidates.json");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << Json({{"diagnostic_only", true}, {"task_filter", m_residualAuditTasks},
        {"snapshot_scope", "CAUSAL_OFF_CANDIDATE_NOT_COMMITTED_PAIR"},
        {"minimum_scope", "EXISTING_MODEL_RESOURCE_FEASIBLE_MINIMUM_NOT_RUNTIME_GUARANTEE"},
        {"records", std::move(records)}}).dump(2) << '\n';
}
} // namespace ns3::protection
