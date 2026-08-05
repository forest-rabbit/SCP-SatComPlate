/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write the logical transfer summary independently from run orchestration.

#include "transfer-metrics.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace ns3
{

namespace
{

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
WriteTransferSummaries(const std::vector<TransferSummaryRecord>& summaries,
                       const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "transfer-summary.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write transfer-summary.csv");
    }
    output << "transfer_id,source_node_id,destination_node_id,source_address,"
              "destination_address,source_port,destination_port,declared_size_bytes,"
              "effective_payload_bytes,pacing_mode,derived_packet_count,"
              "final_packet_payload_bytes,arrival_time_ns,last_send_time_ns,"
              "sent_application_bytes,received_application_bytes,received_packet_count,"
              "completion_time_ns,completion_delay_ns,terminal_state,terminal_reason,"
              "terminal_time_ns,stale_packet_count,capacity_waiting_time_ns\n";
    for (const TransferSummaryRecord& summary : summaries)
    {
        output << summary.transferId << ',' << summary.sourceSatelliteId << ','
               << summary.destinationSatelliteId << ',' << summary.sourceAddress << ','
               << summary.destinationAddress << ',' << summary.sourcePort << ','
               << summary.destinationPort << ',' << summary.declaredSizeBytes << ','
               << summary.payloadBytesPerPacket << ',' << summary.pacingMode << ','
               << summary.derivedPacketCount << ',' << summary.finalPacketPayloadBytes << ','
               << summary.arrivalTimeNs << ',' << summary.lastSendTimeNs << ','
               << summary.sentApplicationBytes << ',' << summary.receivedApplicationBytes << ','
               << summary.receivedPacketCount << ',' << summary.completionTimeNs << ','
               << summary.completionDelayNs << ',' << summary.transferState << ','
               << summary.terminalReason << ',' << summary.terminalTimeNs << ','
               << summary.stalePacketCount << ',' << summary.capacityWaitingTimeNs << '\n';
    }
}

} // namespace ns3
