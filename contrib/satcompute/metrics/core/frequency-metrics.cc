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
           "progress_work,progress_ratio,ffp_local_node,ffp_remote_node,local_free_bytes,remote_"
           "free_bytes,"
           "recovery_rate,input_bandwidth_bytes_per_s,backup_bandwidth_bytes_per_s,j_off,j_start,"
           "selected_score,predicted_recovery_s,predicted_normal_s,rmax_s,t_init_s,"
           "current_delta_permille,current_n,proposed_action,proposed_delta_permille,proposed_n,"
           "actual_fault_sampled,actual_fault_hit,decision_committed,committed_delta_permille,"
           "committed_n,"
           "phase_after,reason,proposal_reason,local_additional_peak_bytes,remote_additional_peak_"
           "bytes\n";
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
            << Phase(in.phase) << ',' << in.risk.qCurrentSample << ',' << in.risk.pFailBeforeFinish
            << ',' << r.progressWork << ',' << in.progress << ',';
        if (r.pair)
            out << r.pair->localNode << ',' << r.pair->remoteNode << ',' << in.localFreeBytes << ','
                << in.remoteFreeBytes << ',' << in.recoveryRate << ',';
        else
            out << ",,,,,";
        number(in.pathAvailable ? std::optional(in.inputBandwidth) : std::nullopt);
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
        out << '\n';
    }
}
} // namespace ns3::protection
