#include "metrics.h"

#include "ns3/abort.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>

namespace ns3 {

namespace {

FlowMonitorHelper g_flowMonitorHelper;

double
SafeDivide(double numerator, double denominator)
{
  return denominator > 0.0 ? numerator / denominator : 0.0;
}

std::string
OutputPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory == ".")
    {
      return filename;
    }
  mkdir(directory.c_str(), 0755);
  return directory.back() == '/' ? directory + filename : directory + "/" + filename;
}

struct FlowAggregate
{
  uint64_t txPackets = 0;
  uint64_t rxPackets = 0;
  uint64_t lostPackets = 0;
  uint64_t txBytes = 0;
  uint64_t rxBytes = 0;
  uint64_t jitterSamples = 0;
  double delaySumSeconds = 0.0;
  double jitterSumSeconds = 0.0;
  double measurementStartSeconds = 0.0;
  double measurementEndSeconds = 0.0;
  bool hasMeasurement = false;

  void Add(const FlowMonitor::FlowStats& flow)
  {
    txPackets += flow.txPackets;
    rxPackets += flow.rxPackets;
    lostPackets += flow.lostPackets;
    txBytes += flow.txBytes;
    rxBytes += flow.rxBytes;
    delaySumSeconds += flow.delaySum.GetSeconds();
    jitterSumSeconds += flow.jitterSum.GetSeconds();
    jitterSamples += flow.rxPackets > 0 ? flow.rxPackets - 1 : 0;

    if (flow.txPackets == 0)
      {
        return;
      }

    double start = flow.timeFirstTxPacket.GetSeconds();
    double end =
      std::max(flow.timeLastTxPacket.GetSeconds(), flow.timeLastRxPacket.GetSeconds());
    if (!hasMeasurement)
      {
        measurementStartSeconds = start;
        measurementEndSeconds = end;
        hasMeasurement = true;
        return;
      }
    measurementStartSeconds = std::min(measurementStartSeconds, start);
    measurementEndSeconds = std::max(measurementEndSeconds, end);
  }

  double MeasurementDurationSeconds() const
  {
    return hasMeasurement
             ? std::max(0.0, measurementEndSeconds - measurementStartSeconds)
             : 0.0;
  }
};

void
PrintNetworkMetrics(const FlowAggregate& metrics)
{
  double duration = metrics.MeasurementDurationSeconds();
  std::cout << "[METRICS] IPv4 network flows" << std::endl
            << "  Tx Packets          : " << metrics.txPackets << std::endl
            << "  Rx Packets          : " << metrics.rxPackets << std::endl
            << "  Lost Packets        : " << metrics.lostPackets << std::endl
            << "  Tx Bytes            : " << metrics.txBytes << std::endl
            << "  Rx Bytes            : " << metrics.rxBytes << std::endl
            << "  Measurement Duration: " << duration << " s" << std::endl
            << "  Mean Delay          : "
            << SafeDivide(metrics.delaySumSeconds, metrics.rxPackets) * 1000.0
            << " ms" << std::endl
            << "  Mean Jitter         : "
            << SafeDivide(metrics.jitterSumSeconds, metrics.jitterSamples) * 1000.0
            << " ms" << std::endl
            << "  Throughput          : "
            << SafeDivide(metrics.rxBytes * 8.0, duration * 1000000.0)
            << " Mbps" << std::endl
            << "  Loss Ratio          : "
            << SafeDivide(metrics.lostPackets * 100.0, metrics.txPackets)
            << " %" << std::endl
            << std::endl;
}

void
WriteNetworkMetrics(const FlowAggregate& metrics, const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "network-flow-metrics.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入网络流指标 CSV");

  double duration = metrics.MeasurementDurationSeconds();
  output << std::setprecision(15)
         << "tx_packets,rx_packets,lost_packets,tx_bytes,rx_bytes,"
            "measurement_start_s,measurement_end_s,measurement_duration_s,"
            "mean_delay_ms,mean_jitter_ms,throughput_mbps,loss_ratio_percent\n"
         << metrics.txPackets << ","
         << metrics.rxPackets << ","
         << metrics.lostPackets << ","
         << metrics.txBytes << ","
         << metrics.rxBytes << ","
         << metrics.measurementStartSeconds << ","
         << metrics.measurementEndSeconds << ","
         << duration << ","
         << SafeDivide(metrics.delaySumSeconds, metrics.rxPackets) * 1000.0 << ","
         << SafeDivide(metrics.jitterSumSeconds, metrics.jitterSamples) * 1000.0 << ","
         << SafeDivide(metrics.rxBytes * 8.0, duration * 1000000.0) << ","
         << SafeDivide(metrics.lostPackets * 100.0, metrics.txPackets)
         << "\n";
}

} // namespace

Ptr<FlowMonitor>
InstallSimulationFlowMonitor()
{
  return g_flowMonitorHelper.InstallAll();
}

MetricsRecorder::MetricsRecorder(Ptr<FlowMonitor> monitor,
                                 double simulationDurationSeconds,
                                 double wallClockSeconds,
                                 const std::string& transportProtocol,
                                 const TaskApplicationMetrics& applicationMetrics,
                                 const std::string& outputDirectory)
  : m_monitor(monitor),
    m_simulationDurationSeconds(simulationDurationSeconds),
    m_wallClockSeconds(wallClockSeconds),
    m_transportProtocol(transportProtocol),
    m_applicationMetrics(applicationMetrics),
    m_outputDirectory(outputDirectory)
{
}

void
MetricsRecorder::Record()
{
  m_monitor->CheckForLostPackets();
  FlowAggregate aggregate;
  for (const auto& flow : m_monitor->GetFlowStats())
    {
      aggregate.Add(flow.second);
    }

  PrintNetworkMetrics(aggregate);
  WriteNetworkMetrics(aggregate, m_outputDirectory);

  std::ofstream output(OutputPath(m_outputDirectory, "task-metrics.json"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入任务级指标 JSON");
  output << std::setprecision(15)
         << "{\n"
         << "  \"simulation_duration_s\": " << m_simulationDurationSeconds << ",\n"
         << "  \"wall_clock_s\": " << m_wallClockSeconds << ",\n"
         << "  \"transport_protocol\": \"" << m_transportProtocol << "\",\n"
         << "  \"sink_applications\": " << m_applicationMetrics.sinkApplications << ",\n"
         << "  \"application_bytes_received\": " << m_applicationMetrics.receivedBytes << "\n"
         << "}\n";

  std::cout << "[METRICS] Output" << std::endl
            << "  network : "
            << OutputPath(m_outputDirectory, "network-flow-metrics.csv") << std::endl
            << "  task    : "
            << OutputPath(m_outputDirectory, "task-metrics.json") << std::endl;
}

} // namespace ns3
