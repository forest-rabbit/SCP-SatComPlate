/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write final capacity-path reservations, rate ledger, and pending count.

#include "capacity-aware-metrics.h"

#include "../../third-party/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

std::filesystem::path
OutputPath(const std::string& directory, const std::string& filename)
{
    const std::filesystem::path root = directory.empty() ? "." : directory;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        throw std::runtime_error("cannot create metrics directory " + root.string() + ": " +
                                 error.message());
    }
    return root / filename;
}

} // namespace

void
WriteCapacityAwareMetrics(const CapacityAwareRuntimeSummary& summary,
                          const std::string& outputDirectory)
{
    const Json content = {
        {"active_path_count_at_end", summary.activePathCountAtEnd},
        {"reserved_directed_link_count_at_end", summary.reservedDirectedLinkCountAtEnd},
        {"total_reserved_rate_bps_at_end", summary.totalReservedRateBpsAtEnd},
        {"pending_transfer_count_at_end", summary.pendingTransferCountAtEnd}};
    std::ofstream output(OutputPath(outputDirectory, "capacity-aware-summary.json"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write capacity-aware-summary.json");
    }
    output << content.dump(2) << '\n';
}

void
RemoveCapacityAwareMetrics(const std::string& outputDirectory)
{
    const std::filesystem::path root = outputDirectory.empty() ? "." : outputDirectory;
    std::error_code error;
    std::filesystem::remove(root / "capacity-aware-summary.json", error);
    if (error)
    {
        throw std::runtime_error("cannot remove stale capacity-aware-summary.json: " +
                                 error.message());
    }
}

} // namespace ns3
