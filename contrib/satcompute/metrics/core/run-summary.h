/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_RUN_SUMMARY_H
#define SATCOMPUTE_RUN_SUMMARY_H

#include "flow-metrics.h"
#include "../../traffic/network-transfer-receiver.h"
#include "../../traffic/network-transfer-records.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ns3
{

class TaskCoordinator;

/** Runtime policy fields retained from the ns-3.33 run-summary contract. */
struct RunMetadata
{
    std::string mode;
    std::string routingMode;
    uint64_t ecmpHashSeed{};
    uint16_t islMtuBytes{};
    uint32_t islQueueBytes{};
    uint32_t receiverRcvBufBytes{};
    bool udpSocketDropCollectionEnabled{};
    std::string diagnosticMode;
    std::string taskCompletionPolicy;
    std::string pacingMode;
    std::string transferChunkMode;
    uint32_t fixedPayloadBytes{};
    std::string computeProfilePath;
    std::string taskTracePath;
};

/** Additional ns-3.48 provenance that must survive legacy metric restoration. */
struct RunSummaryEvidence
{
    int64_t simulationDurationNs{};
    int64_t wallClockNs{};
    std::string workloadMode;
    uint32_t appliedTopologySliceCount{};
    uint32_t routeComputationCount{};
    bool runComplete{};
    bool diagnosticsGenerated{};
};

/** Write the exact legacy field family without extra provenance. */
void WriteRunSummary(const FlowAggregate& aggregate,
                     double simulationDurationSeconds,
                     double wallClockSeconds,
                     const RunMetadata& runMetadata,
                     const ApplicationMetrics& applicationMetrics,
                     const std::vector<TransferSummaryRecord>& transferSummaries,
                     const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
                     const TaskCoordinator* taskCoordinator,
                     const std::string& outputDirectory);

/** Write legacy fields plus deterministic ns-3.48 input/topology evidence. */
void WriteRunSummaryWithEvidence(
    const FlowAggregate& aggregate,
    const RunMetadata& runMetadata,
    const RunSummaryEvidence& evidence,
    const ApplicationMetrics& applicationMetrics,
    const std::vector<TransferSummaryRecord>& transferSummaries,
    const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
    const TaskCoordinator* taskCoordinator,
    const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_RUN_SUMMARY_H
