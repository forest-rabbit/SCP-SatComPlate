/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_NETWORK_TRANSFER_H
#define SATCOMPUTE_NETWORK_TRANSFER_H

#include "../topology/satellite-runtime-view.h"
#include "../topology/satellite-topology.h"
#include "network-transfer-engine.h"
#include "network-transfer-records.h"

#include "ns3/ptr.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ns3
{

/** Legacy-compatible handle for one installed standalone transfer workload. */
struct NetworkTransferState
{
    Ptr<NetworkTransferEngine> engine;
};

/**
 * Install a standalone transfer workload using the ns-3.48 internal time unit.
 */
NetworkTransferState InstallNetworkTransfersNs(
    const std::filesystem::path& filename,
    const std::string& chunkMode,
    uint32_t payloadBytes,
    uint16_t islMtuBytes,
    uint32_t receiverRcvBufBytes,
    bool collectUdpSocketDrops,
    const std::string& logMode,
    int64_t simulationDurationNs,
    SatelliteRuntimeView& topology);

/** Preserve the ns-3.33 public signature; seconds are converted once here. */
NetworkTransferState InstallNetworkTransfers(
    const std::string& filename,
    const std::string& chunkMode,
    uint32_t payloadBytes,
    uint16_t islMtuBytes,
    uint32_t receiverRcvBufBytes,
    bool collectUdpSocketDrops,
    const std::string& logMode,
    double simulationDurationSeconds,
    SatelliteTopology& topology);

ApplicationMetrics CollectNetworkTransferMetrics(const NetworkTransferState& state);

std::vector<TransferFlowMetadata> CollectNetworkTransferFlowMetadata(
    const NetworkTransferState& state);

std::vector<TransferSummaryRecord> CollectNetworkTransferSummaries(
    const NetworkTransferState& state);

} // namespace ns3

#endif
