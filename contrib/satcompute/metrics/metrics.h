#ifndef SATCOMPUTE_METRICS_H
#define SATCOMPUTE_METRICS_H

#include "../jsontopo/topo-link-state.h"
#include "../routing/satcompute-ipv4-global-routing.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

class TaskCoordinator;

struct ApplicationMetrics
{
  uint64_t sinkApplications;
  uint64_t sentBytes;
  uint64_t receivedBytes;
};

struct RunMetadata
{
  std::string mode;
  std::string routingMode;
  uint64_t ecmpHashSeed;
  uint16_t islMtuBytes;
  uint32_t islQueueBytes;
  std::string diagnosticMode;
  std::string pacingMode;
  std::string transferChunkMode;
  uint32_t fixedPayloadBytes;
  std::string computeProfilePath;
  std::string taskTracePath;
};

struct TransferFlowMetadata
{
  uint64_t transferId;
  Ipv4Address sourceAddress;
  Ipv4Address destinationAddress;
  uint8_t protocol;
  uint16_t sourcePort;
  uint16_t destinationPort;
  uint64_t plannedApplicationPayloadBytes;
  uint64_t receivedApplicationPayloadBytes;
};

struct TransferSummaryRecord
{
  uint64_t transferId;
  uint32_t sourceSatelliteId;
  uint32_t destinationSatelliteId;
  Ipv4Address sourceAddress;
  Ipv4Address destinationAddress;
  uint16_t sourcePort;
  uint16_t destinationPort;
  uint64_t declaredSizeBytes;
  uint32_t payloadBytesPerPacket;
  std::string pacingMode;
  uint64_t derivedPacketCount;
  uint32_t finalPacketPayloadBytes;
  int64_t arrivalTimeNs;
  int64_t lastSendTimeNs;
  uint64_t sentApplicationBytes;
  uint64_t receivedApplicationBytes;
  uint64_t receivedPacketCount;
  int64_t completionTimeNs;
  int64_t completionDelayNs;
  std::string transferState;
  uint64_t sentPacketCount;
};

Ptr<FlowMonitor> InstallSimulationFlowMonitor();

class MetricsRecorder
{
public:
  MetricsRecorder(Ptr<FlowMonitor> monitor,
                  double simulationDurationSeconds,
                  double wallClockSeconds,
                  const RunMetadata& runMetadata,
                  const ApplicationMetrics& applicationMetrics,
                  const std::vector<TransferFlowMetadata>& transferFlows,
                  const std::vector<TransferSummaryRecord>& transferSummaries,
                  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
                  const std::vector<IslDirectedLink>& directedLinks,
                  const std::vector<IslQueueDropEvent>& queueDropEvents,
                  const TaskCoordinator* taskCoordinator,
                  const std::string& outputDirectory);

  void Record();

private:
  Ptr<FlowMonitor> m_monitor;
  double m_simulationDurationSeconds;
  double m_wallClockSeconds;
  RunMetadata m_runMetadata;
  ApplicationMetrics m_applicationMetrics;
  std::vector<TransferFlowMetadata> m_transferFlows;
  std::vector<TransferSummaryRecord> m_transferSummaries;
  std::vector<EcmpRouteDecisionEvent> m_routeEvents;
  const std::vector<IslDirectedLink>& m_directedLinks;
  const std::vector<IslQueueDropEvent>& m_queueDropEvents;
  const TaskCoordinator* m_taskCoordinator;
  std::string m_outputDirectory;
};

} // namespace ns3

#endif
