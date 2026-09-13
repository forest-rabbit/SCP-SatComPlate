/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/runtime/n5c-placement-tracker.h"
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace ns3::protection
{
namespace
{
std::string Exact(unsigned __int128 value)
{
    std::string out;
    do { out.push_back('0' + value % 10); value /= 10; } while (value);
    std::reverse(out.begin(), out.end());
    return out;
}
} // namespace
void N5cPlacementTracker::Write(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
    if (m_spatialDiagnostics)
    {
    std::ofstream out(directory / "n5c-placement-decisions.csv");
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << std::setprecision(17);
    out << "decision_id,task_id,time_ns,trigger,variant,reference_local_node,reference_remote_node,"
           "delta_permille,batch_n,candidate_node,selected,committed,resolution,feasible,reason,"
           "feasible_candidate_count,recovery_conflict,first_failure_demand_probability,weighted_conflict,"
           "normal_busy_ns,recovery_busy_ns,exposure_ns,historical_utilization,actual_plus_quota_bytes,"
           "additional_quota_bytes,capacity_bytes,storage_pressure,bottleneck,dominant_dimension,"
           "propagation_ns,tie_break,peer_count,window_count,history_unavailable,no_predicted_demand,"
           "catch_estimate_seconds,recovery_budget_seconds,estimated_ready_after_ns,"
           "reference_recovery_rate,reference_backup_bandwidth,reference_input_bandwidth,"
           "candidate_recovery_rate,candidate_backup_bandwidth,candidate_input_bandwidth,input_local\n";
    for (size_t index = 0; index < m_decisions.size(); ++index)
    {
        const auto& t = m_decisions[index];
        for (size_t i = 0; i < t.candidates.size(); ++i)
        {
            const auto& c = t.candidates[i]; const auto& s = t.selection.scores.at(i);
            out << index << ',' << t.taskId << ',' << t.timeNs << ',' << t.trigger << ','
                << N5cVariantName(m_variant) << ',' << t.reference.localNode << ',' << t.reference.remoteNode
                << ',' << t.config.deltaPermille << ',' << t.config.batchN << ',' << c.remoteNode << ','
                << (t.selection.remoteNode == c.remoteNode) << ',' << t.committed << ',' << t.resolution
                << ',' << s.feasible << ',' << s.reason << ',' << t.selection.feasibleCount << ','
                << s.recoveryConflict << ',' << s.demandProbability << ',' << s.weightedConflict << ','
                << c.normalBusyNs << ',' << c.recoveryBusyNs << ',' << c.exposureNs << ','
                << s.historicalUtilization << ',' << c.accountedBytes << ',' << c.additionalQuotaBytes << ','
                << c.capacityBytes << ',' << s.storagePressure << ',' << s.bottleneck << ',' << s.dominant
                << ',' << s.propagationNs << ',' << t.selection.tieBreak << ',' << s.peerCount << ','
                << s.windowCount << ',' << s.historyUnavailable << ',' << s.noPredictedDemand << ','
                << s.catchSeconds << ',' << s.budgetSeconds << ',' << c.demand.readyAfterNs << ','
                << t.referenceInput.recoveryRate << ',' << t.referenceInput.backupBandwidth << ','
                << t.referenceInput.inputBandwidth << ',' << c.demand.input.recoveryRate << ','
                << c.demand.input.backupBandwidth << ',' << c.demand.input.inputBandwidth << ','
                << c.demand.inputLocal << '\n';
        }
    }
    }
    std::ofstream nodes(directory / "placement-resource-summary.csv");
    nodes.exceptions(std::ios::failbit | std::ios::badbit);
    nodes << std::setprecision(20);
    nodes << "node_id,backup_assignment_count,backup_assignment_time_integral_ns,peak_active_backups,"
             "mean_active_backups,peak_backup_storage_bytes,backup_storage_time_integral_byte_ns,"
             "mean_backup_storage_bytes,normal_busy_ns,recovery_busy_ns,survival_exposure_ns,"
             "historical_utilization,active_backups,storage_bytes\n";
    for (const auto& [node, s] : m_nodes)
    {
        const auto service = Service(node);
        const auto busy = service->GetBusyTimeNs(), recovery = service->GetRecoveryBusyTimeNs();
        const auto exposure = m_faults->ObservedSurvivalExposureNs(node);
        if (busy > exposure || recovery > busy) throw std::logic_error("N5C final history mismatch");
        nodes << node << ',' << s.assignments << ',' << Exact(s.assignmentNs) << ',' << s.peakActive << ','
              << static_cast<long double>(s.assignmentNs) / m_stopNs << ',' << s.peakStorage << ',' << Exact(s.storageByteNs) << ','
              << static_cast<long double>(s.storageByteNs) / m_stopNs << ',' << busy - recovery << ',' << recovery << ','
              << exposure << ',' << (exposure ? static_cast<double>(busy) / exposure : 0) << ','
              << s.active << ',' << s.storageBytes << '\n';
    }
}
} // namespace ns3::protection
