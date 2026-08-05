/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FLOW_METRICS_H
#define SATCOMPUTE_FLOW_METRICS_H

#include "../../traffic/network-transfer-records.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{

/** FlowMonitor counters aggregated across all IPv4 flows in one run. */
struct FlowAggregate
{
    uint64_t txPackets{};
    uint64_t rxPackets{};
    uint64_t lostPackets{};
    uint64_t txBytes{};
    uint64_t rxBytes{};
    uint64_t jitterSamples{};
    std::vector<uint64_t> droppedPacketsByReason;
    std::vector<uint64_t> droppedBytesByReason;
    double delaySumSeconds{};
    double jitterSumSeconds{};
    double measurementStartSeconds{};
    double measurementEndSeconds{};
    bool hasMeasurement{};

    void Add(const FlowMonitor::FlowStats& flow);
    double MeasurementDurationSeconds() const;
    uint64_t ReportedDropPackets() const;
    uint64_t UnattributedLostPackets() const;
};

uint32_t GetIpv4DropReasonCount();
const char* GetIpv4DropReasonName(uint32_t reasonCode);

/** Install the run-wide IPv4 FlowMonitor after applications are installed. */
Ptr<FlowMonitor> InstallSimulationFlowMonitor();

/** Release helper-owned monitor state between independent simulations in one process. */
void ResetSimulationFlowMonitor();

/** Return the classifier owned by the run-wide FlowMonitor helper. */
Ptr<Ipv4FlowClassifier> GetSimulationIpv4FlowClassifier();

FlowAggregate CollectFlowAggregate(Ptr<FlowMonitor> monitor);

void WriteNetworkMetrics(const FlowAggregate& metrics,
                         const std::string& outputDirectory);

void WriteNetworkFlowDetails(Ptr<FlowMonitor> monitor,
                             const std::vector<TransferFlowMetadata>& transferFlows,
                             const std::string& outputDirectory,
                             bool requireCompleteCoverage);

} // namespace ns3

#endif // SATCOMPUTE_FLOW_METRICS_H
