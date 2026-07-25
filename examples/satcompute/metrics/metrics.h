#ifndef SATCOMPUTE_METRICS_H
#define SATCOMPUTE_METRICS_H

#include "ns3/flow-monitor-module.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>

namespace ns3 {

struct TaskApplicationMetrics
{
  uint64_t sinkApplications;
  uint64_t receivedBytes;
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
                  const std::string& outputDirectory);

  void Record();

private:
  Ptr<FlowMonitor> m_monitor;
  double m_simulationDurationSeconds;
  double m_wallClockSeconds;
  std::string m_transportProtocol;
  TaskApplicationMetrics m_applicationMetrics;
  std::string m_outputDirectory;
};

} // namespace ns3

#endif
