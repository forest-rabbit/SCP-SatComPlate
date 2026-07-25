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

#include "network-transfer.h"

#include "ns3/abort.h"
#include "ns3/nstime.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
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
                     uint32_t payloadBytes,
                     uint64_t packetIntervalNs)
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
        transfer.finalPacketPayloadBytes < payloadBytes ? 1 : 0;
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
            << "  payload cap bytes           : " << payloadBytes << std::endl
            << "  derived packet interval ns  : "
            << packetIntervalNs << std::endl
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
                     uint32_t payloadBytes,
                     uint64_t packetIntervalNs,
                     uint16_t islMtuBytes)
{
  std::cout << "[TRANSFER]" << std::endl
            << "  trace              : " << filename << std::endl
            << "  packet payload     : " << payloadBytes << " bytes" << std::endl
            << "  packet interval    : " << packetIntervalNs << " ns" << std::endl
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
                << "  packet_interval_ns : "
                << transfer.derivedPacketIntervalNs << std::endl
                << "  packet_count       : " << transfer.packetCount
                << std::endl
                << "  final_payload_bytes: "
                << transfer.finalPacketPayloadBytes << std::endl
                << "  arrival_time_ns    : " << transfer.arrivalTimeNs
                << std::endl
                << "  last_send_time_ns  : "
                << transfer.lastScheduledSendTimeNs << std::endl
                << "  isl_mtu_bytes      : " << islMtuBytes << std::endl
                << std::endl;
    }
}

} // namespace

NetworkTransferState
InstallNetworkTransfers(const std::string& filename,
                        uint32_t payloadBytes,
                        uint64_t packetIntervalNs,
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
  state.transfers =
    ReadNetworkTransferTrace(filename,
                             payloadBytes,
                             packetIntervalNs,
                             simulationDurationSeconds,
                             topology);

  std::map<uint32_t, Ptr<NetworkTransferReceiver>> receiversBySatellite;
  for (const auto& transfer : state.transfers)
    {
      Ptr<NetworkTransferReceiver>& receiver =
        receiversBySatellite[transfer.destinationSatelliteId];
      if (receiver == nullptr)
        {
          receiver = CreateObject<NetworkTransferReceiver>();
          receiver->Configure(transfer.destinationAddress,
                              transfer.destinationPort);
          topology.GetNodeBySatelliteId(transfer.destinationSatelliteId)
            ->AddApplication(receiver);
          receiver->SetStartTime(Seconds(0.0));
          receiver->SetStopTime(Seconds(simulationDurationSeconds));
          state.receivers.push_back(receiver);
        }
      receiver->AddExpectedTransfer(transfer);
      state.transferReceivers.push_back(receiver);
    }

  for (const auto& transfer : state.transfers)
    {
      Ptr<NetworkTransferApplication> sender =
        CreateObject<NetworkTransferApplication>();
      sender->Configure(transfer);
      topology.GetNodeBySatelliteId(transfer.sourceSatelliteId)
        ->AddApplication(sender);
      sender->SetStartTime(NanoSeconds(transfer.arrivalTimeNs));
      sender->SetStopTime(Seconds(simulationDurationSeconds));
      state.senders.push_back(sender);
    }
  if (logMode == "summary")
    {
      PrintTransferSummary(filename,
                           state.transfers,
                           payloadBytes,
                           packetIntervalNs);
    }
  else if (logMode == "verbose")
    {
      PrintTransferVerbose(filename,
                           state.transfers,
                           payloadBytes,
                           packetIntervalNs,
                           islMtuBytes);
    }
  return state;
}

ApplicationMetrics
CollectNetworkTransferMetrics(const NetworkTransferState& state)
{
  ApplicationMetrics metrics = {};
  metrics.sinkApplications = state.receivers.size();
  NS_ABORT_MSG_IF(state.senders.size() != state.transfers.size(),
                  "NetworkTransfer sender 与配置数量不一致");
  for (uint32_t index = 0; index < state.senders.size(); ++index)
    {
      const Ptr<NetworkTransferApplication>& sender = state.senders[index];
      const NetworkTransfer& transfer = state.transfers[index];
      NS_ABORT_MSG_IF(sender->GetTransferId() != transfer.transferId,
                      "NetworkTransfer sender 顺序与配置不一致");
      NS_ABORT_MSG_IF(sender->GetSentBytes() != transfer.sizeBytes
                        || sender->GetSentPacketCount() != transfer.packetCount,
                      "NetworkTransfer sender 未完成计划 payload，transfer_id="
                        << transfer.transferId);
      metrics.sentBytes =
        CheckedAdd(metrics.sentBytes, sender->GetSentBytes(), "sent bytes");
    }
  for (const auto& receiver : state.receivers)
    {
      metrics.receivedBytes =
        CheckedAdd(metrics.receivedBytes,
                   receiver->GetTotalReceivedBytes(),
                   "received bytes");
    }
  return metrics;
}

std::vector<TransferFlowMetadata>
CollectNetworkTransferFlowMetadata(const NetworkTransferState& state)
{
  NS_ABORT_MSG_IF(state.transfers.size() != state.transferReceivers.size(),
                  "NetworkTransfer 与 receiver 映射数量不一致");
  std::vector<TransferFlowMetadata> metadata;
  metadata.reserve(state.transfers.size());
  for (uint32_t index = 0; index < state.transfers.size(); ++index)
    {
      const NetworkTransfer& transfer = state.transfers[index];
      TransferFlowMetadata flow = {
        transfer.transferId,
        transfer.sourceAddress,
        transfer.destinationAddress,
        17,
        transfer.sourcePort,
        transfer.destinationPort,
        transfer.sizeBytes,
        state.transferReceivers[index]->GetTransferReceivedBytes(
          transfer.transferId)
      };
      metadata.push_back(flow);
    }
  return metadata;
}

std::vector<TransferSummaryRecord>
CollectNetworkTransferSummaries(const NetworkTransferState& state)
{
  NS_ABORT_MSG_IF(state.transfers.size() != state.senders.size()
                    || state.transfers.size()
                         != state.transferReceivers.size(),
                  "NetworkTransfer summary 映射数量不一致");
  std::vector<TransferSummaryRecord> summaries;
  summaries.reserve(state.transfers.size());
  for (uint32_t index = 0; index < state.transfers.size(); ++index)
    {
      const NetworkTransfer& transfer = state.transfers[index];
      const Ptr<NetworkTransferApplication>& sender = state.senders[index];
      const Ptr<NetworkTransferReceiver>& receiver =
        state.transferReceivers[index];
      NS_ABORT_MSG_IF(sender->GetTransferId() != transfer.transferId,
                      "NetworkTransfer summary sender 顺序不一致");

      uint64_t receivedBytes =
        receiver->GetTransferReceivedBytes(transfer.transferId);
      int64_t completionTimeNs =
        receiver->GetTransferCompletionTimeNs(transfer.transferId);
      int64_t completionDelayNs = -1;
      if (completionTimeNs >= 0)
        {
          NS_ABORT_MSG_IF(receivedBytes != transfer.sizeBytes,
                          "已完成 NetworkTransfer 的接收字节不等于声明值，"
                          "transfer_id=" << transfer.transferId);
          completionDelayNs = completionTimeNs - transfer.arrivalTimeNs;
          NS_ABORT_MSG_IF(completionDelayNs < 0,
                          "NetworkTransfer completion delay 为负，transfer_id="
                            << transfer.transferId);
        }

      TransferSummaryRecord summary = {
        transfer.transferId,
        transfer.sourceSatelliteId,
        transfer.destinationSatelliteId,
        transfer.sourceAddress,
        transfer.destinationAddress,
        transfer.sourcePort,
        transfer.destinationPort,
        transfer.sizeBytes,
        transfer.payloadBytesPerPacket,
        transfer.derivedPacketIntervalNs,
        transfer.packetCount,
        transfer.finalPacketPayloadBytes,
        transfer.arrivalTimeNs,
        transfer.lastScheduledSendTimeNs,
        sender->GetSentBytes(),
        receivedBytes,
        receiver->GetTransferReceivedPacketCount(transfer.transferId),
        completionTimeNs,
        completionDelayNs
      };
      summaries.push_back(summary);
    }
  return summaries;
}

} // namespace ns3
