/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-controller.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace ns3::protection::checkbullet
{
CbSatController::CbSatController(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                                 uint64_t capacity, int64_t stopNs,
                                 std::unique_ptr<PlacementPolicy> placement,
                                 RemoteBusyRecoveryPolicy busy)
    : m_tasks(tasks), m_profile(LoadMtbfProfile()), m_placement(std::move(placement)),
      m_manager(tasks, topology, capacity, stopNs, m_profile.mtbfSeconds, *m_placement, m_loads),
      m_recovery(tasks, topology, m_manager, m_loads, stopNs, busy), m_busy(busy)
{
}
void CbSatController::Finalize()
{
    m_recovery.Finalize();
    m_tasks->FinalizeSimulation();
    m_manager.Finalize();
    if (!m_manager.IsQuiescent() || !m_loads.Empty())
        throw std::logic_error("CB-Sat final storage/flow/placement ownership leak");
}
void CbSatController::WriteMetrics(const std::filesystem::path& directory) const
{
    WriteCbMetrics(m_manager, m_recovery, directory);
    m_placement->WriteSelections(directory);
    m_loads.WriteMetrics(directory);
    nlohmann::json j = {
        {"mtbf_seconds", std::isfinite(m_profile.mtbfSeconds) ?
            nlohmann::json(m_profile.mtbfSeconds) : nlohmann::json(nullptr)},
        {"profile_path", MtbfProfilePath().string()}, {"source_execution", m_profile.source},
        {"eligible_exposure_seconds", m_profile.exposureSeconds},
        {"joint_failure_count", m_profile.jointFailures},
        {"eligible_check_count", m_profile.eligibleChecks},
        {"check_interval_ns", m_profile.checkIntervalNs},
        {"placement", m_placement->Name()},
        {"remote_busy_policy", m_busy == RemoteBusyRecoveryPolicy::RECOMPUTE ? "recompute" : "relocate"},
        {"input_contract", "full_original_input"}, {"tail_enabled", false},
        {"restore_cost", "zero_read_one_common_cR_if_logs_nonempty"},
        {"normal_cost", "completed_common_cL_and_cR"},
        {"recovery_cR_accounting", "included_in_reserved_idle_not_added_twice"},
        {"final_quiescent", m_manager.IsQuiescent()}, {"final_loads_empty", m_loads.Empty()},
        {"simultaneous_global_storage_peak_bytes", m_manager.GlobalStoragePeakBytes()}};
    std::ofstream out(directory / "cb-sat-parameters.json");
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << j.dump(2) << '\n';
}
void CbSatController::RemoveOutputs(const std::filesystem::path& directory)
{
    for (const auto name : {"cb-sat-parameters.json", "cb-sat-tasks.csv", "cb-sat-decisions.csv",
            "cb-sat-checkpoints.csv", "cb-sat-events.csv", "cb-sat-recovery.csv",
            "cb-sat-storage.csv", "cb-sat-transfers.csv"})
        std::filesystem::remove(directory / name);
}
} // namespace ns3::protection::checkbullet
