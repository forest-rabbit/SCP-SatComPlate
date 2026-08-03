/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// 写出 capacity-aware 路径预留、速率账本和等待队列的结束状态。

#include "capacity-aware-metrics.h"

#include "ns3/abort.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>

namespace ns3 {

namespace {

std::string
OutputPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory == ".")
    {
      return filename;
    }
  mkdir(directory.c_str(), 0755);
  return directory.back() == '/' ? directory + filename
                                 : directory + "/" + filename;
}

} // namespace

void
WriteCapacityAwareMetrics(const CapacityAwareRuntimeSummary& summary,
                          const std::string& outputDirectory)
{
  std::ofstream output(
    OutputPath(outputDirectory, "capacity-aware-summary.json"),
    std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(),
                  "无法写入 capacity-aware summary JSON");
  output
    << "{\n"
    << "  \"active_path_count_at_end\": "
    << summary.activePathCountAtEnd << ",\n"
    << "  \"reserved_directed_link_count_at_end\": "
    << summary.reservedDirectedLinkCountAtEnd << ",\n"
    << "  \"total_reserved_rate_bps_at_end\": "
    << summary.totalReservedRateBpsAtEnd << ",\n"
    << "  \"pending_transfer_count_at_end\": "
    << summary.pendingTransferCountAtEnd << "\n"
    << "}\n";
}

void
RemoveCapacityAwareMetrics(const std::string& outputDirectory)
{
  std::remove(
    OutputPath(outputDirectory, "capacity-aware-summary.json").c_str());
}

} // namespace ns3
