#include "metrics.h"

#include "ns3/abort.h"
#include "ns3/ipv4-flow-classifier.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
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

void
WriteNetworkFlowDetails(
  Ptr<FlowMonitor> monitor,
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "network-flow-details.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入逐流网络指标 CSV");
  output
    << "flow_monitor_id,transfer_id,source_address,destination_address,"
       "protocol,source_port,destination_port,"
       "planned_application_payload_bytes,received_application_payload_bytes,"
       "tx_packets,rx_packets,lost_packets,tx_bytes,rx_bytes,"
       "time_first_tx_ns,time_last_rx_ns,mean_delay_ns,mean_jitter_ns,"
       "throughput_bps\n";

  std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata> metadataByTuple;
  for (const auto& metadata : transferFlows)
    {
      Ipv4FlowClassifier::FiveTuple tuple = {
        metadata.sourceAddress,
        metadata.destinationAddress,
        metadata.protocol,
        metadata.sourcePort,
        metadata.destinationPort
      };
      NS_ABORT_MSG_IF(
        !metadataByTuple.insert(std::make_pair(tuple, metadata)).second,
        "NetworkTransfer metadata 包含重复 five-tuple，transfer_id="
          << metadata.transferId);
    }

  Ptr<Ipv4FlowClassifier> classifier =
    DynamicCast<Ipv4FlowClassifier>(g_flowMonitorHelper.GetClassifier());
  NS_ABORT_MSG_IF(classifier == nullptr, "FlowMonitor 缺少 IPv4 classifier");
  std::set<uint64_t> matchedTransferIds;
  for (const auto& item : monitor->GetFlowStats())
    {
      FlowId flowId = item.first;
      const FlowMonitor::FlowStats& stats = item.second;
      Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
      auto metadata = metadataByTuple.find(tuple);

      uint64_t transferId = 0;
      uint64_t plannedPayloadBytes = 0;
      uint64_t receivedPayloadBytes = 0;
      if (metadata != metadataByTuple.end())
        {
          transferId = metadata->second.transferId;
          plannedPayloadBytes =
            metadata->second.plannedApplicationPayloadBytes;
          receivedPayloadBytes =
            metadata->second.receivedApplicationPayloadBytes;
          matchedTransferIds.insert(transferId);
        }

      int64_t firstTxNs =
        stats.txPackets > 0 ? stats.timeFirstTxPacket.GetNanoSeconds() : 0;
      int64_t lastRxNs =
        stats.rxPackets > 0 ? stats.timeLastRxPacket.GetNanoSeconds() : 0;
      uint64_t meanDelayNs =
        stats.rxPackets > 0
          ? static_cast<uint64_t>(stats.delaySum.GetNanoSeconds())
              / stats.rxPackets
          : 0;
      uint64_t jitterSamples =
        stats.rxPackets > 0 ? stats.rxPackets - 1 : 0;
      uint64_t meanJitterNs =
        jitterSamples > 0
          ? static_cast<uint64_t>(stats.jitterSum.GetNanoSeconds())
              / jitterSamples
          : 0;
      int64_t durationNs =
        stats.rxPackets > 0 ? std::max<int64_t>(0, lastRxNs - firstTxNs) : 0;
      long double throughputBps =
        durationNs > 0
          ? static_cast<long double>(stats.rxBytes) * 8.0L * 1000000000.0L
              / static_cast<long double>(durationNs)
          : 0.0L;

      output << std::setprecision(15)
             << flowId << ","
             << transferId << ","
             << tuple.sourceAddress << ","
             << tuple.destinationAddress << ","
             << static_cast<uint32_t>(tuple.protocol) << ","
             << tuple.sourcePort << ","
             << tuple.destinationPort << ","
             << plannedPayloadBytes << ","
             << receivedPayloadBytes << ","
             << stats.txPackets << ","
             << stats.rxPackets << ","
             << stats.lostPackets << ","
             << stats.txBytes << ","
             << stats.rxBytes << ","
             << firstTxNs << ","
             << lastRxNs << ","
             << meanDelayNs << ","
             << meanJitterNs << ","
             << static_cast<double>(throughputBps)
             << "\n";
    }

  for (const auto& metadata : transferFlows)
    {
      NS_ABORT_MSG_IF(
        matchedTransferIds.find(metadata.transferId)
          == matchedTransferIds.end(),
        "FlowMonitor 缺少 NetworkTransfer five-tuple，transfer_id="
          << metadata.transferId);
    }
}

void
WriteEcmpRouteEvents(
  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "ecmp-route-events.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 ECMP 路由证据 CSV");
  output
    << "simulation_time_ns,route_epoch,node_id,source_address,"
       "destination_address,protocol,source_port,destination_port,"
       "candidate_count_before_dedup,candidate_count_after_dedup,"
       "selected_index,selected_gateway,selected_output_interface,"
       "hash_value,selection_reason\n";
  for (const auto& event : routeEvents)
    {
      output << event.simulationTimeNs << ","
             << event.routeEpoch << ","
             << event.nodeId << ","
             << event.flowKey.sourceAddress << ","
             << event.flowKey.destinationAddress << ","
             << static_cast<uint32_t>(event.flowKey.protocol) << ","
             << event.flowKey.sourcePort << ","
             << event.flowKey.destinationPort << ","
             << event.candidateCountBeforeDedup << ","
             << event.candidateCountAfterDedup << ","
             << event.selectedIndex << ","
             << event.selectedGateway << ","
             << event.selectedOutputInterface << ","
             << event.hashValue << ","
             << event.selectionReason << "\n";
    }
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
                                 const std::vector<TransferFlowMetadata>& transferFlows,
                                 const std::vector<EcmpRouteDecisionEvent>& routeEvents,
                                 const std::string& outputDirectory)
  : m_monitor(monitor),
    m_simulationDurationSeconds(simulationDurationSeconds),
    m_wallClockSeconds(wallClockSeconds),
    m_transportProtocol(transportProtocol),
    m_applicationMetrics(applicationMetrics),
    m_transferFlows(transferFlows),
    m_routeEvents(routeEvents),
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
  WriteNetworkFlowDetails(m_monitor, m_transferFlows, m_outputDirectory);
  WriteEcmpRouteEvents(m_routeEvents, m_outputDirectory);

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
            << "  details : "
            << OutputPath(m_outputDirectory, "network-flow-details.csv") << std::endl
            << "  ECMP    : "
            << OutputPath(m_outputDirectory, "ecmp-route-events.csv") << std::endl
            << "  task    : "
            << OutputPath(m_outputDirectory, "task-metrics.json") << std::endl;
}

} // namespace ns3
