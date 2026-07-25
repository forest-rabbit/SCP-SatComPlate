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

#include <iostream>
#include <map>

namespace ns3 {

NetworkTransferState
InstallNetworkTransfers(const std::string& filename,
                        uint64_t packetIntervalNs,
                        double simulationDurationSeconds,
                        const SatelliteTopology& topology)
{
  NetworkTransferState state;
  state.transfers =
    ReadNetworkTransferTrace(filename,
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

  std::cout << "[TRANSFER]" << std::endl
            << "  trace              : " << filename << std::endl
            << "  packet payload     : "
            << NETWORK_TRANSFER_PAYLOAD_BYTES << " bytes" << std::endl
            << "  packet interval    : " << packetIntervalNs << " ns"
            << std::endl
            << "  transfers          : " << state.transfers.size()
            << std::endl << std::endl;

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
                << "  packet_count       : " << transfer.packetCount
                << std::endl
                << "  final_payload_bytes: "
                << transfer.finalPacketPayloadBytes << std::endl
                << "  arrival_time_ns    : " << transfer.arrivalTimeNs
                << std::endl
                << "  last_send_time_ns  : "
                << transfer.lastScheduledSendTimeNs << std::endl
                << std::endl;
    }
  return state;
}

TaskApplicationMetrics
CollectNetworkTransferMetrics(const NetworkTransferState& state)
{
  TaskApplicationMetrics metrics = {};
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
    }
  for (const auto& receiver : state.receivers)
    {
      metrics.receivedBytes += receiver->GetTotalReceivedBytes();
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

} // namespace ns3
