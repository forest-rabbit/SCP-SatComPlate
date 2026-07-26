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

// 安装 NetworkTransfer 收发应用，并汇总配置、日志与传输结果。

#include "network-transfer.h"

#include "ns3/abort.h"
#include "ns3/nstime.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>

namespace ns3 {

namespace {

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
  NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                  "NetworkTransfer summary " << field << " 溢出");
  return left + right;
}

void
PrintTransferSample(const std::string& label,
                    const NetworkTransfer& transfer)
{
  std::cout << "  " << label
            << " : id=" << transfer.transferId
            << " " << transfer.sourceSatelliteId
            << "->" << transfer.destinationSatelliteId
            << " size=" << transfer.sizeBytes
            << " packets=" << transfer.packetCount
            << " arrival_ns=" << transfer.arrivalTimeNs
            << std::endl;
}

void
PrintTransferSummary(const std::string& filename,
                     const std::vector<NetworkTransfer>& transfers,
                     const std::string& chunkMode,
                     uint32_t fixedPayloadBytes)
{
  std::set<uint32_t> sources;
  std::set<uint32_t> destinations;
  std::set<std::pair<uint32_t, uint32_t>> pairs;
  uint64_t totalBytes = 0;
  uint64_t totalPackets = 0;
  uint64_t partialFinalPackets = 0;
  uint64_t minBytes = std::numeric_limits<uint64_t>::max();
  uint64_t maxBytes = 0;
  int64_t firstArrivalNs = std::numeric_limits<int64_t>::max();
  int64_t lastArrivalNs = 0;

  for (const auto& transfer : transfers)
    {
      sources.insert(transfer.sourceSatelliteId);
      destinations.insert(transfer.destinationSatelliteId);
      pairs.insert(std::make_pair(transfer.sourceSatelliteId,
                                  transfer.destinationSatelliteId));
      totalBytes = CheckedAdd(totalBytes, transfer.sizeBytes, "bytes");
      totalPackets =
        CheckedAdd(totalPackets, transfer.packetCount, "packet count");
      partialFinalPackets +=
        transfer.finalPacketPayloadBytes
            < transfer.payloadBytesPerPacket
          ? 1
          : 0;
      minBytes = std::min(minBytes, transfer.sizeBytes);
      maxBytes = std::max(maxBytes, transfer.sizeBytes);
      firstArrivalNs = std::min(firstArrivalNs, transfer.arrivalTimeNs);
      lastArrivalNs = std::max(lastArrivalNs, transfer.arrivalTimeNs);
    }

  uint64_t estimatedHeaderBytes = 0;
  NS_ABORT_MSG_IF(totalPackets
                    > std::numeric_limits<uint64_t>::max() / 28u,
                  "NetworkTransfer estimated header bytes 溢出");
  estimatedHeaderBytes = totalPackets * 28u;
  long double meanBytes =
    transfers.empty()
      ? 0.0L
      : static_cast<long double>(totalBytes) / transfers.size();

  std::cout << "[TRANSFER:SUMMARY]" << std::endl
            << "  trace                       : " << filename << std::endl
            << "  transfers                   : " << transfers.size() << std::endl
            << "  unique sources              : " << sources.size() << std::endl
            << "  unique destinations         : " << destinations.size() << std::endl
            << "  unique source-dest pairs    : " << pairs.size() << std::endl
            << "  total application bytes     : " << totalBytes << std::endl
            << "  min/mean/max transfer bytes : "
            << minBytes << "/" << meanBytes << "/" << maxBytes << std::endl
            << "  first/last arrival ns       : "
            << firstArrivalNs << "/" << lastArrivalNs << std::endl
            << "  chunk mode                  : " << chunkMode << std::endl;
  if (chunkMode == "fixed")
    {
      std::cout << "  fixed payload cap bytes     : "
                << fixedPayloadBytes << std::endl;
    }
  std::cout
            << "  pacing mode                 : first-hop-serialization"
            << std::endl
            << "  total derived packets       : " << totalPackets << std::endl
            << "  final-packet count          : " << transfers.size() << std::endl
            << "  partial final packets       : "
            << partialFinalPackets << std::endl
            << "  estimated UDP+IPv4 headers  : "
            << estimatedHeaderBytes << " bytes" << std::endl;

  uint32_t sampleCount =
    std::min<uint32_t>(3, static_cast<uint32_t>(transfers.size()));
  for (uint32_t index = 0; index < sampleCount; ++index)
    {
      PrintTransferSample("first[" + std::to_string(index) + "]",
                          transfers[index]);
    }
  uint32_t lastStart =
    transfers.size() > sampleCount ? transfers.size() - sampleCount : 0;
  for (uint32_t index = lastStart; index < transfers.size(); ++index)
    {
      PrintTransferSample(
        "last[" + std::to_string(index - lastStart) + "]",
        transfers[index]);
    }
  std::cout << std::endl;
}

void
PrintTransferVerbose(const std::string& filename,
                     const std::vector<NetworkTransfer>& transfers,
                     const std::string& chunkMode,
                     uint32_t fixedPayloadBytes,
                     uint16_t islMtuBytes)
{
  std::cout << "[TRANSFER]" << std::endl
            << "  trace              : " << filename << std::endl
            << "  chunk mode         : " << chunkMode << std::endl;
  if (chunkMode == "fixed")
    {
      std::cout << "  fixed payload      : "
                << fixedPayloadBytes << " bytes" << std::endl;
    }
  std::cout
            << "  pacing mode        : first-hop-serialization" << std::endl
            << "  ISL MTU            : " << islMtuBytes << " bytes" << std::endl
            << "  transfers          : " << transfers.size()
            << std::endl << std::endl;

  for (const auto& transfer : transfers)
    {
      std::cout << "[TRANSFER:" << transfer.transferId << "]" << std::endl
                << "  source             : sat_id="
                << transfer.sourceSatelliteId << " "
                << transfer.sourceAddress << ":" << transfer.sourcePort
                << std::endl
                << "  destination        : sat_id="
                << transfer.destinationSatelliteId << " "
                << transfer.destinationAddress << ":"
                << transfer.destinationPort << std::endl
                << "  size_bytes         : " << transfer.sizeBytes
                << std::endl
                << "  payload_cap_bytes  : "
                << transfer.payloadBytesPerPacket << std::endl
                << "  packet_count       : " << transfer.packetCount
                << std::endl
                << "  final_payload_bytes: "
                << transfer.finalPacketPayloadBytes << std::endl
                << "  arrival_time_ns    : " << transfer.arrivalTimeNs
                << std::endl
                << "  isl_mtu_bytes      : " << islMtuBytes << std::endl
                << std::endl;
    }
}

void
StartStandaloneTransfer(Ptr<NetworkTransferEngine> engine,
                        uint64_t transferId)
{
  engine->StartTransferNow(
    transferId,
    Callback<void, uint64_t, int64_t>());
}

} // namespace

NetworkTransferState
InstallNetworkTransfers(const std::string& filename,
                        const std::string& chunkMode,
                        uint32_t payloadBytes,
                        uint16_t islMtuBytes,
                        const std::string& logMode,
                        double simulationDurationSeconds,
                        const SatelliteTopology& topology)
{
  NS_ABORT_MSG_IF(logMode != "summary"
                    && logMode != "verbose"
                    && logMode != "silent",
                  "未知 transferLogMode: " << logMode);
  NetworkTransferState state;
  std::vector<NetworkTransfer> plans =
    ReadNetworkTransferTrace(filename,
                             simulationDurationSeconds,
                             topology);
  state.engine = CreateObject<NetworkTransferEngine>();
  state.engine->Configure(topology,
                          chunkMode,
                          payloadBytes,
                          islMtuBytes,
                          simulationDurationSeconds);
  state.engine->RegisterPlans(plans);
  const std::vector<NetworkTransfer>& preparedPlans =
    state.engine->GetPlans();
  for (const auto& plan : preparedPlans)
    {
      NS_ABORT_MSG_IF(plan.arrivalTimeNs < 0,
                      "standalone NetworkTransfer 缺少 arrival_time_ns，"
                      "transfer_id=" << plan.transferId);
      Simulator::Schedule(
        NanoSeconds(plan.arrivalTimeNs),
        &StartStandaloneTransfer,
        state.engine,
        plan.transferId);
    }
  if (logMode == "summary")
    {
      PrintTransferSummary(filename,
                           preparedPlans,
                           chunkMode,
                           payloadBytes);
    }
  else if (logMode == "verbose")
    {
      PrintTransferVerbose(filename,
                           preparedPlans,
                           chunkMode,
                           payloadBytes,
                           islMtuBytes);
    }
  return state;
}

ApplicationMetrics
CollectNetworkTransferMetrics(const NetworkTransferState& state)
{
  NS_ABORT_MSG_IF(state.engine == nullptr,
                  "NetworkTransferState 缺少 engine");
  return state.engine->CollectApplicationMetrics();
}

std::vector<TransferFlowMetadata>
CollectNetworkTransferFlowMetadata(const NetworkTransferState& state)
{
  NS_ABORT_MSG_IF(state.engine == nullptr,
                  "NetworkTransferState 缺少 engine");
  return state.engine->CollectFlowMetadata();
}

std::vector<TransferSummaryRecord>
CollectNetworkTransferSummaries(const NetworkTransferState& state)
{
  NS_ABORT_MSG_IF(state.engine == nullptr,
                  "NetworkTransferState 缺少 engine");
  return state.engine->CollectSummaries();
}

} // namespace ns3
