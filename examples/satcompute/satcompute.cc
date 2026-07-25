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

#include "metrics/metrics.h"
#include "para.h"
#include "topo.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SatCompute");

namespace {

constexpr uint32_t TRAFFIC_TIME_SLICES = 100;
constexpr uint32_t TRAFFIC_PACKET_SIZE_BYTES = 1024;
constexpr uint32_t LEGACY_UDP_PACKET_DIVISOR = 10000;
constexpr double LEGACY_UDP_WINDOW_SECONDS = 100.0;
constexpr long double BITS_PER_GIBIBIT = 1073741824.0L;

struct ApplicationState
{
  std::vector<Ptr<PacketSink>> sinks;
  uint32_t clientCount = 0;
  uint64_t plannedPacketCount = 0;
};

std::vector<std::vector<double>>
ReadTrafficMatrix(const std::string& filename, uint32_t nodeCount)
{
  std::ifstream input(filename);
  NS_ABORT_MSG_IF(!input.is_open(), "无法打开业务流量矩阵: " << filename);
  NS_ABORT_MSG_IF(nodeCount == 0, "业务流量矩阵要求至少一个卫星节点");

  const uint64_t expectedRows =
    static_cast<uint64_t>(nodeCount) * TRAFFIC_TIME_SLICES;
  std::vector<std::vector<double>> matrix(
    nodeCount,
    std::vector<double>(nodeCount, 0.0));
  std::string line;
  uint32_t lineNumber = 0;
  uint64_t rowCount = 0;
  while (std::getline(input, line))
    {
      ++lineNumber;
      if (line.empty())
        {
          continue;
        }

      std::vector<double> row;
      std::stringstream stream(line);
      std::string token;
      while (std::getline(stream, token, ','))
        {
          try
            {
              size_t parsedCharacters = 0;
              double value = std::stod(token, &parsedCharacters);
              bool onlyWhitespaceRemains =
                std::all_of(token.begin() + parsedCharacters,
                            token.end(),
                            [](char character) {
                              return character == ' ' || character == '\t'
                                     || character == '\r';
                            });
              NS_ABORT_MSG_IF(!onlyWhitespaceRemains
                                || !std::isfinite(value)
                                || value < 0.0,
                              "流量矩阵包含无效值，行 " << lineNumber
                              << ": " << filename);
              row.push_back(value);
            }
          catch (const std::exception& error)
            {
              NS_FATAL_ERROR("流量矩阵解析失败，行 " << lineNumber
                             << ": " << error.what()
                             << "\nfile: " << filename);
            }
        }

      NS_ABORT_MSG_IF(row.size() != nodeCount,
                      "流量矩阵第 " << lineNumber << " 行包含 " << row.size()
                      << " 列，期望 " << nodeCount << " 列");
      NS_ABORT_MSG_IF(rowCount >= expectedRows,
                      "流量矩阵非空行数超过 " << expectedRows
                      << ": " << filename);

      uint32_t source = static_cast<uint32_t>(rowCount % nodeCount);
      for (uint32_t destination = 0; destination < nodeCount; ++destination)
        {
          double accumulated = matrix[source][destination] + row[destination];
          NS_ABORT_MSG_IF(!std::isfinite(accumulated),
                          "流量矩阵累计值溢出，源节点下标: " << source
                          << "，目的节点下标: " << destination);
          matrix[source][destination] = accumulated;
        }
      ++rowCount;
    }

  NS_ABORT_MSG_IF(rowCount != expectedRows,
                  "流量矩阵包含 " << rowCount
                  << " 个非空行，期望 " << expectedRows << " 行");
  return matrix;
}

uint32_t
CalculateLegacyUdpPacketCount(double demandGbps, double offeredLoad)
{
  long double packetCount =
    static_cast<long double>(demandGbps)
    * static_cast<long double>(offeredLoad)
    * BITS_PER_GIBIBIT
    / (static_cast<long double>(TRAFFIC_PACKET_SIZE_BYTES) * 8.0L)
    / static_cast<long double>(LEGACY_UDP_PACKET_DIVISOR);
  NS_ABORT_MSG_IF(packetCount > std::numeric_limits<uint32_t>::max(),
                  "旧版 UDP 计划包数超出 uint32 范围");
  return std::max(1u, static_cast<uint32_t>(packetCount));
}

ApplicationState
InstallApplications(const SatComputeConfig& config,
                    const SatelliteTopology& topology)
{
  ApplicationState state;
  const uint16_t servicePort = 9;
  const std::string socketFactory =
    config.transport == "tcp" ? "ns3::TcpSocketFactory" : "ns3::UdpSocketFactory";
  const double applicationStart =
    std::min(0.1, config.simulationDurationSeconds / 10.0);

  for (uint32_t index = 0; index < topology.GetNodeCount(); ++index)
    {
      PacketSinkHelper sink(socketFactory,
                            InetSocketAddress(Ipv4Address::GetAny(), servicePort));
      ApplicationContainer application = sink.Install(topology.GetNode(index));
      state.sinks.push_back(DynamicCast<PacketSink>(application.Get(0)));
      application.Start(Seconds(0.0));
      application.Stop(Seconds(config.simulationDurationSeconds));
    }

  if (config.offeredLoad == 0.0)
    {
      std::cout << "[TRAFFIC] offeredLoad=0; no client flows installed"
                << std::endl << std::endl;
      return state;
    }

  std::vector<std::vector<double>> matrix =
    ReadTrafficMatrix(config.trafficMatrix, topology.GetNodeCount());
  for (uint32_t source = 0; source < topology.GetNodeCount(); ++source)
    {
      for (uint32_t destination = 0;
           destination < topology.GetNodeCount();
           ++destination)
        {
          double demandGbps = matrix[source][destination];
          if (demandGbps == 0.0)
            {
              continue;
            }

          Ipv4Address destinationAddress =
            topology.GetServiceAddress(destination);
          ApplicationContainer application;
          if (config.transport == "udp")
            {
              uint32_t maxPackets =
                CalculateLegacyUdpPacketCount(demandGbps, config.offeredLoad);
              double intervalSeconds =
                LEGACY_UDP_WINDOW_SECONDS / maxPackets;
              UdpClientHelper client(destinationAddress, servicePort);
              client.SetAttribute("MaxPackets", UintegerValue(maxPackets));
              client.SetAttribute("Interval",
                                  TimeValue(Seconds(intervalSeconds)));
              client.SetAttribute("PacketSize",
                                  UintegerValue(TRAFFIC_PACKET_SIZE_BYTES));
              application = client.Install(topology.GetNode(source));
              application.Start(Seconds(0.0));
              application.Stop(
                Seconds(std::min(LEGACY_UDP_WINDOW_SECONDS,
                                 config.simulationDurationSeconds)));
              state.plannedPacketCount += maxPackets;
            }
          else
            {
              long double rate =
                static_cast<long double>(demandGbps)
                * static_cast<long double>(config.offeredLoad)
                * 1000000000.0L;
              NS_ABORT_MSG_IF(rate > std::numeric_limits<uint64_t>::max(),
                              "业务流速率超出 uint64 范围");
              uint64_t rateBps = static_cast<uint64_t>(rate + 0.5L);
              if (rateBps == 0)
                {
                  continue;
                }

              Address remote =
                InetSocketAddress(destinationAddress, servicePort);
              OnOffHelper client(socketFactory, remote);
              client.SetConstantRate(DataRate(rateBps),
                                     TRAFFIC_PACKET_SIZE_BYTES);
              client.SetAttribute(
                "OnTime",
                StringValue("ns3::ConstantRandomVariable[Constant=1]"));
              client.SetAttribute(
                "OffTime",
                StringValue("ns3::ConstantRandomVariable[Constant=0]"));
              application = client.Install(topology.GetNode(source));
              application.Start(Seconds(applicationStart));
              application.Stop(Seconds(config.simulationDurationSeconds));
            }
          ++state.clientCount;
        }
    }

  std::cout << "[TRAFFIC]" << std::endl
            << "  matrix  : " << config.trafficMatrix << std::endl
            << "  slices  : " << TRAFFIC_TIME_SLICES << std::endl
            << "  model   : "
            << (config.transport == "udp"
                  ? "legacy bounded UDP"
                  : "continuous TCP OnOff")
            << std::endl
            << "  clients : " << state.clientCount << std::endl;
  if (config.transport == "udp")
    {
      std::cout << "  packets : " << state.plannedPacketCount
                << " planned over " << LEGACY_UDP_WINDOW_SECONDS << " s"
                << std::endl;
    }
  std::cout << std::endl;
  return state;
}

TaskApplicationMetrics
CollectApplicationMetrics(const ApplicationState& applications)
{
  TaskApplicationMetrics metrics = {};
  metrics.sinkApplications = applications.sinks.size();
  for (const auto& sink : applications.sinks)
    {
      metrics.receivedBytes += sink->GetTotalRx();
    }
  return metrics;
}

} // namespace

int
main(int argc, char* argv[])
{
  SatComputeConfig config = GetDefaultSatComputeConfig();
  CommandLine commandLine;
  commandLine.AddValue("topologyDir",
                       "Directory containing paired nodes_<time>s.json and "
                       "topology_<time>s.json snapshots",
                       config.topologyDirectory);
  commandLine.AddValue("simulationDuration",
                       "Simulation duration in seconds",
                       config.simulationDurationSeconds);
  commandLine.AddValue("offeredLoad",
                       "Multiplier applied to the traffic matrix",
                       config.offeredLoad);
  commandLine.AddValue("transport",
                       "Application transport: udp or tcp",
                       config.transport);
  commandLine.AddValue("trafficMatrix",
                       "100N-row by N-column traffic input in Gbps",
                       config.trafficMatrix);
  commandLine.AddValue("outputDir",
                       "Metrics output directory",
                       config.outputDirectory);
  commandLine.Parse(argc, argv);

  std::transform(config.transport.begin(),
                 config.transport.end(),
                 config.transport.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  if (!std::isfinite(config.simulationDurationSeconds)
      || config.simulationDurationSeconds <= 0.0)
    {
      std::cerr << "[RUN:Error] simulationDuration must be a finite positive number"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (!std::isfinite(config.offeredLoad) || config.offeredLoad < 0.0)
    {
      std::cerr << "[RUN:Error] offeredLoad must be a finite non-negative number"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.transport != "udp" && config.transport != "tcp")
    {
      std::cerr << "[RUN:Error] transport must be udp or tcp" << std::endl;
      return EXIT_FAILURE;
    }

  std::cout << "[RUN]" << std::endl
            << "  topologyDir       : " << config.topologyDirectory << std::endl
            << "  simulationDuration: " << config.simulationDurationSeconds << " s"
            << std::endl
            << "  offeredLoad       : " << config.offeredLoad << std::endl
            << "  transport         : " << config.transport << std::endl
            << "  trafficMatrix     : " << config.trafficMatrix << std::endl
            << "  outputDir         : " << config.outputDirectory << std::endl
            << "  routing           : ns-3 Ipv4GlobalRouting" << std::endl
            << std::endl;

  std::chrono::steady_clock::time_point wallClockStart =
    std::chrono::steady_clock::now();

  TopologyConfig topologyConfig = {
    config.topologyDirectory,
    config.simulationDurationSeconds
  };
  SatelliteTopology topology(topologyConfig);
  topology.Initialize();
  ApplicationState applications = InstallApplications(config, topology);
  Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();

  Simulator::Stop(Seconds(config.simulationDurationSeconds));
  Simulator::Run();

  double wallClockSeconds =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - wallClockStart)
      .count();
  std::cout << "[RUN] wall-clock: " << wallClockSeconds << " s"
            << std::endl << std::endl;

  MetricsRecorder metrics(flowMonitor,
                          config.simulationDurationSeconds,
                          wallClockSeconds,
                          config.transport,
                          CollectApplicationMetrics(applications),
                          config.outputDirectory);
  metrics.Record();
  Simulator::Destroy();
  return EXIT_SUCCESS;
}
