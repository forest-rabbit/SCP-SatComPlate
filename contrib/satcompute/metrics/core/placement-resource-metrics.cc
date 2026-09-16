/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/runtime/placement-resource-tracker.h"
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
void PlacementResourceTracker::WriteResources(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
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
        if (busy > exposure || recovery > busy) throw std::logic_error("placement final history mismatch");
        nodes << node << ',' << s.assignments << ',' << Exact(s.assignmentNs) << ',' << s.peakActive << ','
              << static_cast<long double>(s.assignmentNs) / m_stopNs << ',' << s.peakStorage << ',' << Exact(s.storageByteNs) << ','
              << static_cast<long double>(s.storageByteNs) / m_stopNs << ',' << busy - recovery << ',' << recovery << ','
              << exposure << ',' << (exposure ? static_cast<double>(busy) / exposure : 0) << ','
              << s.active << ',' << s.storageBytes << '\n';
    }
}
} // namespace ns3::protection
