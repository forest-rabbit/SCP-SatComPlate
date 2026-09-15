/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/policy/compfrr/compfrr-controller.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace ns3::protection
{
namespace
{
using Json = nlohmann::json;
/** Serialize already evaluated scalar inputs; never invoke storageDemand or a solver. */
Json ResourceInputs(const FrequencyInput& in)
{
    return {{"base_transfer_s", in.baseTransferSeconds}, {"state_transfer_s", in.stateTransferSeconds},
            {"input_bytes", in.inputBytes}, {"total_work", in.work}, {"progress_fraction", in.progress},
            {"remaining_seconds", in.remainingSeconds}, {"deadline_ns", in.deadlineNs},
            {"input_bandwidth_bytes_per_s", in.inputBandwidth}, {"backup_bandwidth_bytes_per_s", in.backupBandwidth},
            {"recovery_rate", in.recoveryRate}, {"primary_rate", in.primaryRate},
            {"local_free_bytes", in.localFreeBytes}, {"remote_free_bytes", in.remoteFreeBytes},
            {"Kvar_bytes", in.variableBytes}, {"cL_ns", in.costs.localNs}, {"cR_ns", in.costs.remoteNs},
            {"node_available", in.nodeAvailable}, {"path_available", in.pathAvailable},
            {"replay_available", in.replayAvailable}};
}
} // namespace

void CompFrrController::WriteInputStartAudit(const std::filesystem::path& directory) const
{
    if (!m_inputStartAudit && !m_optionalInput) return;
    Json records = Json::array();
    for (const auto& r : m_inputStartRecords)
    {
        Json path = {{"source_node", r.task.sourceNodeId}, {"remote_node", r.pair.remoteNode},
                     {"local_delivery", r.inputPath.local}, {"source_available", r.sourceAvailable},
                     {"remote_available", r.remoteAvailable}, {"reachable", r.inputPath.reachable},
                     {"admissible", r.inputPath.admissible}, {"failure_reason", r.inputPath.failureReason},
                     {"admitted_rate_bps", r.inputPath.local || !r.inputPath.admissible ? Json(nullptr) : Json(r.inputPath.path.admittedRateBps)},
                     {"propagation_ns", r.inputPath.admissible ? Json(r.inputPath.propagationNs) : Json(nullptr)},
                     {"hops", Json::array()}};
        for (const auto& hop : r.inputPath.path.hops)
            path["hops"].push_back({{"source", hop.sourceSatelliteId}, {"destination", hop.destinationSatelliteId},
                                    {"output_interface", hop.candidate.outputInterface}, {"link_rate_bps", hop.linkRateBps}});
        Json prediction = nullptr;
        if (r.prediction)
        {
            prediction = {{"prediction_time_ns", r.timeNs}, {"remaining_compute_ns", r.remainingNs},
                          {"first_sample_time_ns", r.firstSampleNs}, {"finish_exclusive", r.finishExclusive},
                          {"check_interval_ns", r.intervalNs}, {"source", "QueryTaskPrediction"},
                          {"first_sample_semantics", r.firstSampleNs == r.timeNs ? "PENDING_CURRENT" : "NEXT_CANONICAL"},
                          {"q_current_snapshot", r.prediction->combinedStepFailureProbability},
                          {"P_F", r.prediction->predictedFailureProbability},
                          {"future_steps_count", r.prediction->steps.size()}, {"future_steps", Json::array()}};
            for (const auto& step : r.prediction->steps)
                prediction["future_steps"].push_back({{"time_ns", step.targetTimeNs},
                    {"q_f1", step.f1StepFailureProbability}, {"q_f2", step.f2StepFailureProbability},
                    {"q_comp", step.combinedStepFailureProbability}});
        }
        Json actual = nullptr;
        if (r.actualValidation)
        {
            actual = ResourceInputs(r.actualValidation->input);
            actual["local_additional_peak_bytes"] = r.actualValidation->storage.localAdditionalBytes;
            actual["remote_additional_peak_bytes"] = r.actualValidation->storage.remoteAdditionalBytes;
        }
        const auto& d = r.referenceProposal;
        records.push_back({{"task_id", r.task.taskId}, {"profile", TaskProfileToString(r.task.taskProfile)},
            {"input_bytes", r.task.inputBytes}, {"compute_work_units", r.task.computeWorkUnits},
            {"source_node", r.task.sourceNodeId}, {"primary_node", r.task.computeNodeId},
            {"local_node", r.pair.localNode}, {"remote_node", r.pair.remoteNode},
            {"start_time_ns", r.timeNs}, {"start_trigger", r.trigger}, {"admitted", true},
            {"snapshot_point", "PRE_START_CHECKPOINT"}, {"checkpoint_ready", false},
            {"progress_work", r.progressWork}, {"progress_fraction", double(r.progressWork)/r.task.computeWorkUnits},
            {"remaining_compute_ns", r.remainingNs}, {"deadline_ns", r.deadlineNs},
            {"deadline_slack_ns", r.deadlineNs-r.timeNs-r.remainingNs},
            {"delta_permille", r.config.deltaPermille}, {"batch_n", r.config.batchN},
            {"primary_rate", r.primaryRate}, {"recovery_rate", r.recoveryRate},
            {"physical_local_free_bytes", r.localFreeBytes}, {"physical_remote_free_bytes", r.remoteFreeBytes},
            {"Kvar_bytes", r.referenceInput.variableBytes}, {"record_header_bytes", r.headerBytes},
            {"legal_initial_work", r.legalWork}, {"initial_variable_state_bytes", r.initialVariableStateBytes},
            {"cL_ns", r.referenceInput.costs.localNs}, {"cR_ns", r.referenceInput.costs.remoteNs},
            {"input_staging_policy", r.referenceInput.inputPolicy == InputStagingPolicy::DEFERRED ? "deferred" : "eager"},
            {"predictor", prediction}, {"input_path", path}, {"actual_post_batch_validation", actual},
            {"frequency_reference_pair", r.referencePair ? Json{{"local", r.referencePair->localNode},
                                                                 {"remote", r.referencePair->remoteNode}} : Json(nullptr)},
            {"frequency_proposal_inputs", ResourceInputs(r.referenceInput)},
            {"frequency_proposal_estimates", {{"initialization_s", d.initializationSeconds},
                {"initialization_ready_time_ns", d.initReadyTimeNs ? Json(*d.initReadyTimeNs) : Json(nullptr)},
                {"p_fail_after_init_ready", d.pFailAfterInitReady ? Json(*d.pFailAfterInitReady) : Json(nullptr)},
                {"normal_maintenance_s", d.selected ? Json(d.selected->normalSeconds) : Json(nullptr)},
                {"average_recovery_s", d.selected ? Json(d.selected->averageRecoverySeconds) : Json(nullptr)},
                {"local_additional_peak_bytes", d.selected ? Json(d.selected->storage.localAdditionalBytes) : Json(nullptr)},
                {"remote_additional_peak_bytes", d.selected ? Json(d.selected->storage.remoteAdditionalBytes) : Json(nullptr)}}}});
    }
    std::filesystem::create_directories(directory);
    std::ofstream stream(directory / "input-start-snapshots.json");
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << Json({{"purpose", "DEVELOPMENT_CALIBRATION"}, {"selective_input_enabled", bool(m_optionalInput)},
                    {"unadmitted_snapshot_count", m_inputStartRejected}, {"candidates", records}}).dump(2) << '\n';
}
} // namespace ns3::protection
