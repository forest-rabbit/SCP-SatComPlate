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

#ifndef SATCOMPUTE_NETWORK_TRANSFER_H
#define SATCOMPUTE_NETWORK_TRANSFER_H

#include "../metrics/metrics.h"
#include "../topo.h"
#include "network-transfer-config.h"
#include "network-transfer-engine.h"

#include "ns3/ptr.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

struct NetworkTransferState
{
  Ptr<NetworkTransferEngine> engine;
};

NetworkTransferState InstallNetworkTransfers(
  const std::string& filename,
  const std::string& chunkMode,
  uint32_t payloadBytes,
  uint16_t islMtuBytes,
  const std::string& logMode,
  double simulationDurationSeconds,
  const SatelliteTopology& topology);

ApplicationMetrics CollectNetworkTransferMetrics(
  const NetworkTransferState& state);

std::vector<TransferFlowMetadata> CollectNetworkTransferFlowMetadata(
  const NetworkTransferState& state);

std::vector<TransferSummaryRecord> CollectNetworkTransferSummaries(
  const NetworkTransferState& state);

} // namespace ns3

#endif
