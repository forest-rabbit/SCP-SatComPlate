/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/runtime/frequency-protection-controller.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
const char* Phase(ProtectionPhase phase)
{
    switch (phase)
    {
    case ProtectionPhase::OFF:
        return "OFF";
    case ProtectionPhase::INITIALIZING:
        return "INITIALIZING";
    case ProtectionPhase::ON:
        return "ON";
    case ProtectionPhase::RECOVERING:
        return "RECOVERING";
    case ProtectionPhase::DONE:
        return "DONE";
    }
    throw std::logic_error("unknown protection phase");
}

const char* Action(FrequencyAction action)
{
    switch (action)
    {
    case FrequencyAction::NONE:
        return "NONE";
    case FrequencyAction::START:
        return "START";
    case FrequencyAction::UPDATE:
        return "UPDATE";
    case FrequencyAction::PAUSE:
        return "PAUSE";
    }
    throw std::logic_error("unknown frequency action");
}
} // namespace

void FrequencyProtectionController::WriteDecisions(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
    std::ofstream out(directory / "frequency-decisions.csv");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(17);
    out << "task_id,task_profile,fault_epoch_time_ns,phase_before,q_current_sample,p_fail_before_"
           "finish,"
           "progress_work,progress_ratio,local_node,remote_node,local_free_bytes,remote_"
           "free_bytes,"
           "recovery_rate,input_bandwidth_bytes_per_s,backup_bandwidth_bytes_per_s,j_off,j_start,"
           "selected_score,predicted_recovery_s,predicted_normal_s,rmax_s,t_init_s,"
           "current_delta_permille,current_n,proposed_action,proposed_delta_permille,proposed_n,"
           "actual_fault_sampled,actual_fault_hit,decision_committed,committed_delta_permille,"
           "committed_n,"
           "phase_after,reason,proposal_reason,local_additional_peak_bytes,remote_additional_peak_"
           "bytes,placement_mode,resource_reason,local_active_backup_assignments,"
           "remote_active_backup_assignments,local_active_recoveries,remote_active_recoveries,"
           "decision_trigger,first_sample_time_ns,p_f1_snapshot,p_f2_snapshot,q_comp_snapshot,"
           "replay_available,replay_reason,waiting_capacity_before,waiting_capacity_after,"
           "pair_candidates_total,pair_node_feasible,pair_path_feasible,pair_hard_checked,"
           "pair_hard_feasible,pair_skip_node,pair_skip_no_route,pair_skip_no_capacity,pair_skip_other,"
           "pair_skip_storage,pair_skip_deadline,pair_hard_rejection_reason,capacity_retry_count,"
           "capacity_retry_success,capacity_wait_start_ns,capacity_wait_end_ns,capacity_wait_duration_ns\n";
    auto number = [&](const auto& value) {
        if (value)
            out << *value;
        out << ',';
    };
    auto configuration = [&](const auto& config) {
        if (config)
            out << config->deltaPermille;
        out << ',';
        if (config)
            out << config->batchN;
        out << ',';
    };
    for (const auto& r : m_decisions)
    {
        const auto& in = r.input;
        const auto& d = r.proposal;
        out << r.taskId << ',' << TaskProfileToString(r.profile) << ',' << in.risk.epochNs << ','
            << Phase(in.phase) << ',';
        if (r.trigger == "FAULT_EPOCH")
            out << in.risk.qCurrentSample;
        out << ',' << in.risk.pFailBeforeFinish << ',' << r.progressWork << ',' << in.progress
            << ',';
        if (r.pair)
            out << r.pair->localNode << ',' << r.pair->remoteNode << ',' << in.localFreeBytes << ','
                << in.remoteFreeBytes << ',' << in.recoveryRate << ',';
        else
            out << ",,,,,";
        number(in.pathAvailable && in.replayAvailable ? std::optional(in.inputBandwidth)
                                                      : std::nullopt);
        number(in.pathAvailable ? std::optional(in.backupBandwidth) : std::nullopt);
        number(in.phase == ProtectionPhase::OFF ? d.jOff : std::nullopt);
        number(d.jStart);
        number(d.selected ? std::optional(d.selected->objective) : std::nullopt);
        number(d.selected ? std::optional(d.selected->averageRecoverySeconds) : std::nullopt);
        number(d.selected ? std::optional(d.selected->normalSeconds) : std::nullopt);
        number(in.pathAvailable ? std::optional(d.deadlineSlackSeconds) : std::nullopt);
        number(in.phase == ProtectionPhase::OFF && in.pathAvailable
                   ? std::optional(d.initializationSeconds)
                   : std::nullopt);
        configuration(r.previous);
        out << Action(d.action) << ',';
        configuration(d.selected ? std::optional(d.selected->config) : std::nullopt);
        out << r.sampled << ',' << r.faultHit << ',' << r.committed << ',';
        configuration(r.committedConfig);
        out << Phase(r.phaseAfter) << ',' << r.reason << ',' << d.reason << ',';
        number(d.selected ? std::optional(d.selected->storage.localAdditionalBytes) : std::nullopt);
        if (d.selected)
            out << d.selected->storage.remoteAdditionalBytes;
        out << ',' << m_placement->Name() << ',' << r.resourceReason << ',';
        if (r.pair)
            out << r.localLoad.activeBackup << ',' << r.remoteLoad.activeBackup << ','
                << r.localLoad.activeRecovery << ',' << r.remoteLoad.activeRecovery;
        else out << ",,,";
        out << ',' << r.trigger << ',' << r.firstSampleNs << ',' << r.pF1 << ',' << r.pF2 << ','
            << in.risk.qCurrentSample << ',' << in.replayAvailable << ',' << r.replayReason << ','
            << r.waitingBefore << ',' << r.waitingAfter << ',';
        if (in.phase == ProtectionPhase::OFF)
            out << r.pairStats.total << ',' << r.pairStats.nodeFeasible << ',' << r.pairPathFeasible
                << ',' << r.pairHardChecked << ',' << r.pairHardFeasible << ',' << r.pairStats.skipNode
                << ',' << r.pairStats.skipNoRoute << ',' << r.pairStats.skipNoCapacity << ','
                << r.pairStats.skipOther << ',' << r.pairSkipStorage << ',' << r.pairSkipDeadline;
        else out << ",,,,,,,,,,";
        out << ',';
        if (r.pairSkipStorage || r.pairSkipDeadline)
            out << "PAIR_REJECTED_BY_FREQUENCY_HARD_CONSTRAINT";
        out << ',' << r.capacityRetryCount << ',' << r.capacityRetrySuccess << ',';
        number(r.capacityWaitStartNs >= 0 ? std::optional(r.capacityWaitStartNs) : std::nullopt);
        number(r.capacityWaitEndNs >= 0 ? std::optional(r.capacityWaitEndNs) : std::nullopt);
        if (r.capacityWaitStartNs >= 0 && r.capacityWaitEndNs >= 0)
            out << r.capacityWaitEndNs - r.capacityWaitStartNs;
        out << '\n';
    }
    std::ofstream pauses(directory / "frequency-pause-intervals.csv");
    pauses.exceptions(std::ios::badbit | std::ios::failbit);
    pauses << "task_id,start_time_ns,end_time_ns,duration_ns,reason\n";
    for (const auto& p : m_pauses)
        pauses << p.taskId << ',' << p.startNs << ',' << p.endNs << ',' << p.endNs-p.startNs << ',' << p.reason << '\n';
    std::ofstream waits(directory / "frequency-capacity-waits.csv");
    waits.exceptions(std::ios::badbit | std::ios::failbit);
    waits << "task_id,start_time_ns,end_time_ns,duration_ns,reason\n";
    for (const auto& w : m_capacityWaits)
        waits << w.taskId << ',' << w.startNs << ',' << w.endNs << ',' << w.endNs-w.startNs
              << ',' << w.reason << '\n';
    std::ofstream risks(directory / "f3-compute-risk-snapshots.csv");
    risks.exceptions(std::ios::badbit | std::ios::failbit);
    risks << std::setprecision(17)
          << "time_ns,node_id,task_id,p_f1_snapshot,p_f2_snapshot,q_comp_snapshot,"
             "p_fail_before_finish,F1F2_sampled,actual_fault\n";
    for (const auto& r : m_faults->GetF3ComputeRiskRecords())
        risks << r.timeNs << ',' << r.nodeId << ',' << r.taskId << ',' << r.pF1 << ',' << r.pF2
              << ',' << r.qCompute << ',' << r.pFinish << ",0,F3\n";
}
} // namespace ns3::protection
