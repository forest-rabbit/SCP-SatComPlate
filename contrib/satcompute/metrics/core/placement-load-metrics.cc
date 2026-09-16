/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/runtime/placement-load-ledger.h"

#include <fstream>

namespace ns3::protection
{
void PlacementLoadLedger::WriteMetrics(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
    std::ofstream nodes(directory / "placement-node-summary.csv");
    nodes.exceptions(std::ios::failbit | std::ios::badbit);
    nodes << "node_id,backup_assignment_count_total,peak_active_backup_assignments,"
             "accepted_recovery_count,peak_active_recoveries,active_backup_assignments,active_"
             "recoveries\n";
    for (const auto& [node, load] : m_nodes)
        nodes << node << ',' << load.totalBackup << ',' << load.peakBackup << ','
              << load.totalRecovery << ',' << load.peakRecovery << ',' << load.activeBackup << ','
              << load.activeRecovery << '\n';
    std::ofstream events(directory / "placement-load-events.csv");
    events.exceptions(std::ios::failbit | std::ios::badbit);
    events << "task_id,node_id,time_ns,event,active_backup_assignments,active_recoveries\n";
    for (const auto& e : m_events)
        events << e.taskId << ',' << e.node << ',' << e.timeNs << ',' << e.event << ','
               << e.load.activeBackup << ',' << e.load.activeRecovery << '\n';
}
} // namespace ns3::protection
