#ifndef SATCOMPUTE_METRICS_H
#define SATCOMPUTE_METRICS_H

#include "../routing/satcompute-ipv4-global-routing.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

struct TaskApplicationMetrics
{
  uint64_t sinkApplications;
  uint64_t receivedBytes;
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

Ptr<FlowMonitor> InstallSimulationFlowMonitor();

class MetricsRecorder
{
public:
  MetricsRecorder(Ptr<FlowMonitor> monitor,
                  double simulationDurationSeconds,
                  double wallClockSeconds,
                  const std::string& transportProtocol,
                  const TaskApplicationMetrics& applicationMetrics,
                  const std::vector<TransferFlowMetadata>& transferFlows,
                  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
                  const std::string& outputDirectory);

  void Record();

private:
  Ptr<FlowMonitor> m_monitor;
  double m_simulationDurationSeconds;
  double m_wallClockSeconds;
  std::string m_transportProtocol;
  TaskApplicationMetrics m_applicationMetrics;
  std::vector<TransferFlowMetadata> m_transferFlows;
  std::vector<EcmpRouteDecisionEvent> m_routeEvents;
  std::string m_outputDirectory;
};

} // namespace ns3

#endif
