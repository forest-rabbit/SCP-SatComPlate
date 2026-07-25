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

#include "metrics.h"

#include "../para.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sys/stat.h>

namespace ns3 {

static FlowMonitorHelper flowmonHelper;

static double
SafeDivide(double numerator, double denominator)
{
  return denominator > 0.0 ? numerator / denominator : 0.0;
}

static std::string
MetricsOutputPath(const std::string& outputDirectory, const std::string& filename)
{
  if (outputDirectory.empty() || outputDirectory == ".")
  {
    return filename;
  }
  mkdir(outputDirectory.c_str(), 0755);
  return outputDirectory.back() == '/' ? outputDirectory + filename
                                       : outputDirectory + "/" + filename;
}

struct FlowAggregate
{
  uint64_t tx_packets = 0;
  uint64_t rx_packets = 0;
  uint64_t lost_packets = 0;
  uint64_t tx_bytes = 0;
  uint64_t rx_bytes = 0;
  uint64_t jitter_samples = 0;
  double delay_sum_s = 0.0;
  double jitter_sum_s = 0.0;
  double measurement_start_s = 0.0;
  double measurement_end_s = 0.0;
  bool has_measurement = false;

  void Add(const FlowMonitor::FlowStats& flow)
  {
    tx_packets += flow.txPackets;
    rx_packets += flow.rxPackets;
    lost_packets += flow.lostPackets;
    tx_bytes += flow.txBytes;
    rx_bytes += flow.rxBytes;
    delay_sum_s += flow.delaySum.GetSeconds();
    jitter_sum_s += flow.jitterSum.GetSeconds();
    jitter_samples += flow.rxPackets > 0 ? flow.rxPackets - 1 : 0;

    if (flow.txPackets == 0)
    {
      return;
    }
    double start = flow.timeFirstTxPacket.GetSeconds();
    double end = std::max(flow.timeLastTxPacket.GetSeconds(),
                          flow.timeLastRxPacket.GetSeconds());
    if (!has_measurement)
    {
      measurement_start_s = start;
      measurement_end_s = end;
      has_measurement = true;
      return;
    }
    measurement_start_s = std::min(measurement_start_s, start);
    measurement_end_s = std::max(measurement_end_s, end);
  }

  double MeasurementDurationSeconds() const
  {
    return has_measurement ? std::max(0.0, measurement_end_s - measurement_start_s) : 0.0;
  }
};

static void
PrintNetworkMetrics(const std::string& label, const FlowAggregate& metrics)
{
  double duration = metrics.MeasurementDurationSeconds();
  std::cout << "[METRICS] " << label << std::endl
            << "  Tx Packets: " << metrics.tx_packets << " p\n"
            << "  Tx Bytes: " << metrics.tx_bytes << " bytes\n"
            << "  Rx Packets: " << metrics.rx_packets << " p\n"
            << "  Rx Bytes: " << metrics.rx_bytes << " bytes\n"
            << "  Lost Packets: " << metrics.lost_packets << " p\n"
            << "  Measurement Duration: " << duration << " s\n"
            << "  Mean Delay: " << SafeDivide(metrics.delay_sum_s, metrics.rx_packets) * 1000.0 << " ms\n"
            << "  Mean Jitter: " << SafeDivide(metrics.jitter_sum_s, metrics.jitter_samples) * 1000.0 << " ms\n"
            << "  Throughput: " << SafeDivide(metrics.rx_bytes * 8.0, duration * 1000000.0) << " Mbps\n"
            << "  Loss Packet Ratio: " << SafeDivide(metrics.lost_packets * 100.0, metrics.tx_packets) << " %\n"
            << std::endl;
}

static void
WriteFlowCsvRow(std::ostream& output, const std::string& category, const FlowAggregate& metrics)
{
  double duration = metrics.MeasurementDurationSeconds();
  output << category << ","
         << metrics.tx_packets << "," << metrics.rx_packets << "," << metrics.lost_packets << ","
         << metrics.tx_bytes << "," << metrics.rx_bytes << ","
         << metrics.measurement_start_s << "," << metrics.measurement_end_s << "," << duration << ","
         << SafeDivide(metrics.delay_sum_s, metrics.rx_packets) * 1000.0 << ","
         << SafeDivide(metrics.jitter_sum_s, metrics.jitter_samples) * 1000.0 << ","
         << SafeDivide(metrics.rx_bytes * 8.0, duration * 1000000.0) << ","
         << SafeDivide(metrics.lost_packets * 100.0, metrics.tx_packets) << "\n";
}

static void
WriteNetworkMetricsCsv(const FlowAggregate& business,
                       const FlowAggregate& control,
                       const std::string& outputDirectory)
{
  std::ofstream output(MetricsOutputPath(outputDirectory, "network-flow-metrics.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入网络流指标CSV");
  output << std::setprecision(15)
         << "category,tx_packets,rx_packets,lost_packets,tx_bytes,rx_bytes,"
            "measurement_start_s,measurement_end_s,measurement_duration_s,"
            "mean_delay_ms,mean_jitter_ms,throughput_mbps,loss_ratio_percent\n";
  WriteFlowCsvRow(output, "business", business);
  WriteFlowCsvRow(output, "control", control);
}

static void
WriteTaskMetricsJson(const TaskApplicationMetrics& stats,
                     double wallClockSeconds,
                     const std::string& outputDirectory)
{
  std::ofstream output(MetricsOutputPath(outputDirectory, "task-metrics.json"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入任务级指标JSON");
  output << std::setprecision(15)
         << "{\n"
         << "  \"simulation_duration_s\": " << totalTimeStep << ",\n"
         << "  \"wall_clock_s\": " << wallClockSeconds << ",\n"
         << "  \"transport_protocol\": \""
         << (_tranProc == 1 ? "TCP" : "UDP") << "\",\n"
         << "  \"server_applications\": " << stats.server_applications << ",\n"
         << "  \"udp_packets_received\": " << stats.udp_packets_received << ",\n"
         << "  \"tcp_bytes_received\": " << stats.tcp_bytes_received << "\n"
         << "}\n";
}

Ptr<FlowMonitor>
InstallSimulationFlowMonitor()
{
  return flowmonHelper.InstallAll();
}

MetricsRecorder::MetricsRecorder(Ptr<FlowMonitor> monitor,
                                 double wallClockSeconds,
                                 const TaskApplicationMetrics& applicationMetrics,
                                 const std::string& outputDirectory)
  : m_monitor(monitor),
    m_wallClockSeconds(wallClockSeconds),
    m_applicationMetrics(applicationMetrics),
    m_outputDirectory(outputDirectory)
{
}

void
MetricsRecorder::Record()
{
  m_monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> stats = m_monitor->GetFlowStats();
  FlowAggregate business;
  FlowAggregate control;
  for (const auto& item : stats)
  {
    if (item.second.packetPrio == 0)
    {
      control.Add(item.second);
    }
    else if (item.second.packetPrio == 1)
    {
      business.Add(item.second);
    }
  }

  PrintNetworkMetrics("业务网络流", business);
  PrintNetworkMetrics("控制网络流", control);

  WriteNetworkMetricsCsv(business, control, m_outputDirectory);
  WriteTaskMetricsJson(m_applicationMetrics, m_wallClockSeconds, m_outputDirectory);
  std::cout << "[METRICS] 结构化结果" << std::endl
            << "  Network Flows: "
            << MetricsOutputPath(m_outputDirectory, "network-flow-metrics.csv") << "\n"
            << "  Task: " << MetricsOutputPath(m_outputDirectory, "task-metrics.json") << "\n";
}

} // namespace ns3
