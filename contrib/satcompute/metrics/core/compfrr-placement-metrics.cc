/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/policy/compfrr/placement/compfrr-placement-tracker.h"
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace ns3::protection
{
void CompFrrPlacementTracker::Write(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
    if (m_spatialDiagnostics)
    {
    std::ofstream out(directory / "compfrr-placement-decisions.csv");
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
                << CompFrrPlacementVariantName(m_variant) << ',' << t.reference.localNode << ',' << t.reference.remoteNode
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
    if (m_spatialDiagnostics && m_variant == CompFrrPlacementVariant::RECENT_U)
    {
        std::ofstream recent(directory / "compfrr-recent-u-history.csv");
        recent.exceptions(std::ios::failbit | std::ios::badbit);
        recent << std::setprecision(17);
        recent << "decision_id,task_id,time_ns,candidate_node,horizon_ns,window_begin_ns,window_end_ns,"
                  "normal_busy_ns,recovery_busy_ns,exposure_ns,cumulative_utilization,recent_utilization,"
                  "history_unavailable\n";
        for (size_t index = 0; index < m_decisions.size(); ++index)
        {
            const auto& t = m_decisions[index];
            for (const auto& c : t.candidates)
                recent << index << ',' << t.taskId << ',' << t.timeNs << ',' << c.remoteNode << ','
                    << c.historyHorizonNs << ',' << c.historyWindowBeginNs << ',' << c.historyWindowEndNs << ','
                    << c.recentNormalBusyNs << ',' << c.recentRecoveryBusyNs << ',' << c.recentExposureNs << ','
                    << (c.exposureNs ? static_cast<double>(c.normalBusyNs + c.recoveryBusyNs) / c.exposureNs : 0)
                    << ',' << (c.recentExposureNs ?
                        static_cast<double>(c.recentNormalBusyNs + c.recentRecoveryBusyNs) / c.recentExposureNs : 0)
                    << ',' << (c.recentExposureNs == 0) << '\n';
        }
    }
    if (m_spatialDiagnostics && m_variant == CompFrrPlacementVariant::RATIONAL_U)
    {
        std::ofstream rational(directory / "compfrr-rational-u-history.csv");
        rational.exceptions(std::ios::failbit | std::ios::badbit);
        rational << std::setprecision(17);
        rational << "decision_id,task_id,time_ns,candidate_node,horizon_ns,continuous_idle_ns,"
                    "freshness,cumulative_utilization,rational_pressure,history_unavailable\n";
        for (size_t index = 0; index < m_decisions.size(); ++index)
        {
            const auto& t = m_decisions[index];
            for (const auto& c : t.candidates)
            {
                const double global = c.exposureNs ?
                    static_cast<double>(c.normalBusyNs + c.recoveryBusyNs) / c.exposureNs : 0;
                rational << index << ',' << t.taskId << ',' << t.timeNs << ',' << c.remoteNode << ','
                    << c.historyHorizonNs << ',' << c.continuousIdleNs << ','
                    << IdleAwareComputePressure(1, c.historyHorizonNs, c.continuousIdleNs) << ',' << global << ','
                    << IdleAwareComputePressure(global, c.historyHorizonNs, c.continuousIdleNs) << ','
                    << (c.exposureNs == 0) << '\n';
            }
        }
    }
    WriteResources(directory);
}
} // namespace ns3::protection
