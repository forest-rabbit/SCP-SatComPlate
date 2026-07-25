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

struct RunConfig
{
  std::string topologyDirectory =
    "examples/satcompute/input/topology/json/examples/xw-66sat";
  std::string trafficMatrix =
    "examples/satcompute/input/traffic/traffic_matrix(66).csv";
  std::string outputDirectory = "examples/satcompute/output";
  std::string transport = "udp";
  double simulationDurationSeconds = 110.0;
  double offeredLoad = 0.0;
};

struct ApplicationState
{
  std::vector<Ptr<PacketSink>> sinks;
  uint32_t clientCount = 0;
};

std::vector<std::vector<double>>
ReadTrafficMatrix(const std::string& filename, uint32_t nodeCount)
{
  std::ifstream input(filename);
  NS_ABORT_MSG_IF(!input.is_open(), "无法打开业务流量矩阵: " << filename);

  std::vector<std::vector<double>> matrix;
  std::string line;
  uint32_t lineNumber = 0;
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
      matrix.push_back(row);
    }

  NS_ABORT_MSG_IF(matrix.size() != nodeCount,
                  "流量矩阵包含 " << matrix.size()
                  << " 行，期望 " << nodeCount << " 行");
  for (uint32_t index = 0; index < nodeCount; ++index)
    {
      NS_ABORT_MSG_IF(matrix[index][index] != 0.0,
                      "流量矩阵对角线必须为 0，节点下标: " << index);
    }
  return matrix;
}

ApplicationState
InstallApplications(const RunConfig& config, const SatelliteTopology& topology)
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
            InetSocketAddress(topology.GetServiceAddress(destination), servicePort);
          OnOffHelper client(socketFactory, remote);
          client.SetConstantRate(DataRate(rateBps), 1024);
          client.SetAttribute("OnTime",
                              StringValue("ns3::ConstantRandomVariable[Constant=1]"));
          client.SetAttribute("OffTime",
                              StringValue("ns3::ConstantRandomVariable[Constant=0]"));

          ApplicationContainer application =
            client.Install(topology.GetNode(source));
          application.Start(Seconds(applicationStart));
          application.Stop(Seconds(config.simulationDurationSeconds));
          ++state.clientCount;
        }
    }

  std::cout << "[TRAFFIC]" << std::endl
            << "  matrix  : " << config.trafficMatrix << std::endl
            << "  clients : " << state.clientCount << std::endl
            << std::endl;
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
  RunConfig config;
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
                       "NxN traffic matrix in Gbps",
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
