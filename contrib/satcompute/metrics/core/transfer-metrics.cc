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

// 写出逻辑传输摘要。

#include "transfer-metrics.h"

#include "ns3/abort.h"

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
  return directory.back() == '/' ? directory + filename : directory + "/" + filename;
}

} // namespace

void
WriteTransferSummaries(
  const std::vector<TransferSummaryRecord>& summaries,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "transfer-summary.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 transfer summary CSV");
  output
    << "transfer_id,source_node_id,destination_node_id,source_address,"
       "destination_address,source_port,destination_port,declared_size_bytes,"
       "effective_payload_bytes,pacing_mode,"
       "derived_packet_count,final_packet_payload_bytes,arrival_time_ns,"
       "last_send_time_ns,sent_application_bytes,"
       "received_application_bytes,received_packet_count,completion_time_ns,"
       "completion_delay_ns\n";
  for (const auto& summary : summaries)
    {
      output << summary.transferId << ","
             << summary.sourceSatelliteId << ","
             << summary.destinationSatelliteId << ","
             << summary.sourceAddress << ","
             << summary.destinationAddress << ","
             << summary.sourcePort << ","
             << summary.destinationPort << ","
             << summary.declaredSizeBytes << ","
             << summary.payloadBytesPerPacket << ","
             << summary.pacingMode << ","
             << summary.derivedPacketCount << ","
             << summary.finalPacketPayloadBytes << ","
             << summary.arrivalTimeNs << ","
             << summary.lastSendTimeNs << ","
             << summary.sentApplicationBytes << ","
             << summary.receivedApplicationBytes << ","
             << summary.receivedPacketCount << ","
             << summary.completionTimeNs << ","
             << summary.completionDelayNs << "\n";
    }
}

} // namespace ns3
