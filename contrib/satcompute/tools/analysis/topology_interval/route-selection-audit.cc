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

// 用稳定五元组触发真实 C++ 选路，并导出每个 route epoch 的下一跳证据。

#include "route-audit-common.h"

#include "../../../metrics/routing/ecmp-route-recorder.h"
#include "../../../routing/ns3/satcompute-ipv4-global-routing-helper.h"
#include "../../../third-party/nlohmann/json.hpp"
#include "../../../topology/satellite-topology.h"

#include "ns3/abort.h"
#include "ns3/core-module.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/udp-header.h"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;
using namespace ns3;

namespace {

struct ProbePair
{
  uint32_t sourceId;
  uint32_t destinationId;
  uint16_t sourcePort;
  uint16_t destinationPort;
  std::string category;
};

uint32_t
ParseUint32(const json& value,
            const std::string& field,
            const std::string& filename)
{
  try
    {
      if (value.is_number_unsigned())
        {
          uint64_t parsed = value.get<uint64_t>();
          NS_ABORT_MSG_IF(parsed > std::numeric_limits<uint32_t>::max(),
                          field << " 超出 uint32_t: " << filename);
          return static_cast<uint32_t>(parsed);
        }
      if (value.is_number_integer())
        {
          int64_t parsed = value.get<int64_t>();
          NS_ABORT_MSG_IF(parsed < 0,
                          field << " 必须是非负整数: " << filename);
          NS_ABORT_MSG_IF(
            static_cast<uint64_t>(parsed)
              > std::numeric_limits<uint32_t>::max(),
            field << " 超出 uint32_t: " << filename);
          return static_cast<uint32_t>(parsed);
        }
    }
  catch (const std::exception& error)
    {
      NS_ABORT_MSG("无法解析 " << field << ": " << error.what()
                               << "\nfile: " << filename);
    }
  NS_ABORT_MSG(field << " 必须是非负整数: " << filename);
  return 0;
}

std::vector<ProbePair>
ReadProbePairs(const std::string& filename)
{
  std::ifstream input(filename.c_str());
  NS_ABORT_MSG_IF(!input, "无法打开 probe pair JSON: " << filename);
  json root;
  try
    {
      input >> root;
    }
  catch (const std::exception& error)
    {
      NS_ABORT_MSG("无法解析 probe pair JSON: " << error.what()
                                                << "\nfile: " << filename);
    }
  NS_ABORT_MSG_IF(!root.is_object() || root.size() != 2,
                  "probe pair root 字段不符合合同: " << filename);
  NS_ABORT_MSG_IF(
    root.find("schema_version") == root.end()
      || root["schema_version"] != "0.1"
      || root.find("probe_pairs") == root.end()
      || !root["probe_pairs"].is_array(),
    "probe pair root 内容不符合合同: " << filename);
  const json& entries = root["probe_pairs"];
  NS_ABORT_MSG_IF(entries.empty() || entries.size() > 64,
                  "probe_pairs 数量必须在 1..64: " << filename);

  std::vector<ProbePair> pairs;
  std::set<std::pair<uint32_t, uint32_t>> endpoints;
  std::set<std::pair<uint16_t, uint16_t>> ports;
  for (const auto& entry : entries)
    {
      NS_ABORT_MSG_IF(!entry.is_object() || entry.size() != 5,
                      "probe pair 字段不符合合同: " << filename);
      for (const auto* field : {
             "source_id",
             "destination_id",
             "source_port",
             "destination_port",
             "category"
           })
        {
          NS_ABORT_MSG_IF(entry.find(field) == entry.end(),
                          "probe pair 缺少字段 " << field
                                                << ": " << filename);
        }
      uint32_t sourceId =
        ParseUint32(entry["source_id"], "source_id", filename);
      uint32_t destinationId =
        ParseUint32(entry["destination_id"], "destination_id", filename);
      uint32_t sourcePort =
        ParseUint32(entry["source_port"], "source_port", filename);
      uint32_t destinationPort =
        ParseUint32(entry["destination_port"],
                    "destination_port",
                    filename);
      NS_ABORT_MSG_IF(sourceId == destinationId,
                      "probe pair 源和目的不能相同: " << filename);
      NS_ABORT_MSG_IF(sourcePort == 0 || sourcePort > 65535
                        || destinationPort == 0
                        || destinationPort > 65535,
                      "probe pair 端口必须在 1..65535: " << filename);
      NS_ABORT_MSG_IF(!entry["category"].is_string()
                        || entry["category"].get<std::string>().empty(),
                      "probe pair category 必须是非空字符串: "
                        << filename);
      NS_ABORT_MSG_IF(
        !endpoints.insert(std::make_pair(sourceId, destinationId)).second,
        "probe pair 包含重复有序节点对: " << filename);
      NS_ABORT_MSG_IF(
        !ports.insert(
           std::make_pair(static_cast<uint16_t>(sourcePort),
                          static_cast<uint16_t>(destinationPort))).second,
        "probe pair 包含重复端口对: " << filename);
      pairs.push_back(
        {
          sourceId,
          destinationId,
          static_cast<uint16_t>(sourcePort),
          static_cast<uint16_t>(destinationPort),
          entry["category"].get<std::string>()
        });
    }
  return pairs;
}

std::string
AddressString(Ipv4Address address)
{
  std::ostringstream output;
  output << address;
  return output.str();
}

void
WriteSelectionSnapshot(SatelliteTopology* topology,
                       EcmpRouteRecorder* recorder,
                       const std::vector<ProbePair>* pairs,
                       std::ostream* output,
                       const std::string* routingMode,
                       uint32_t timeSeconds)
{
  NS_ABORT_MSG_IF(topology == nullptr
                    || recorder == nullptr
                    || pairs == nullptr
                    || output == nullptr
                    || routingMode == nullptr,
                  "route selection audit callback 参数为空");
  for (const auto& pair : *pairs)
    {
      NS_ABORT_MSG_IF(
        !topology->HasSatelliteId(pair.sourceId)
          || !topology->HasSatelliteId(pair.destinationId),
        "probe pair 引用了不存在的卫星");
      Ptr<Node> source = topology->GetNodeBySatelliteId(pair.sourceId);
      Ptr<SatComputeIpv4GlobalRouting> routing =
        SatComputeIpv4GlobalRoutingHelper::GetRouting(source);
      std::vector<uint32_t> candidates =
        topology->GetEcmpCandidateSatelliteIds(pair.sourceId,
                                               pair.destinationId);

      Ptr<Packet> packet = Create<Packet>(1);
      UdpHeader udp;
      udp.SetSourcePort(pair.sourcePort);
      udp.SetDestinationPort(pair.destinationPort);
      packet->AddHeader(udp);
      Ipv4Header header;
      header.SetSource(
        topology->GetServiceAddressBySatelliteId(pair.sourceId));
      header.SetDestination(
        topology->GetServiceAddressBySatelliteId(pair.destinationId));
      header.SetProtocol(17);
      header.SetPayloadSize(packet->GetSize());

      std::size_t eventCountBefore = recorder->GetEvents().size();
      Socket::SocketErrno socketError = Socket::ERROR_NOTERROR;
      Ptr<Ipv4Route> route =
        routing->RouteOutput(packet, header, nullptr, socketError);
      std::size_t eventCountAfter = recorder->GetEvents().size();
      bool expectEvent = *routingMode != "global-first";
      NS_ABORT_MSG_IF(
        eventCountAfter != eventCountBefore + (expectEvent ? 1 : 0),
        "route selection audit 的 EcmpRouteDecision 事件数量不正确");

      const EcmpRouteDecisionEvent* event =
        expectEvent ? &recorder->GetEvents().back() : nullptr;
      int64_t selectedNextHopId = -1;
      int64_t selectedOutputInterface = -1;
      Ipv4Address selectedGateway = Ipv4Address::GetAny();
      if (route != nullptr)
        {
          Ptr<Ipv4> ipv4 = source->GetObject<Ipv4>();
          int32_t interface =
            ipv4->GetInterfaceForDevice(route->GetOutputDevice());
          NS_ABORT_MSG_IF(interface < 0,
                          "selected route 缺少 output interface");
          selectedOutputInterface = interface;
          selectedGateway = route->GetGateway();
          selectedNextHopId =
            topology->GetNextHopSatelliteId(
              pair.sourceId,
              static_cast<uint32_t>(interface));
        }
      if (event != nullptr && event->selectedIndex >= 0)
        {
          NS_ABORT_MSG_IF(
            route == nullptr
              || event->selectedOutputInterface
                   != selectedOutputInterface
              || event->selectedGateway != selectedGateway,
            "EcmpRouteDecision 与 RouteOutput 的最终出口不一致");
        }
      NS_ABORT_MSG_IF(
        (route == nullptr) != candidates.empty(),
        "effective candidates 与实际 route reachability 不一致");

      json row = {
        {"simulation_time_ns", Simulator::Now().GetNanoSeconds()},
        {"time_s", timeSeconds},
        {"route_epoch", routing->GetRouteEpoch()},
        {"routing_mode", *routingMode},
        {"source_id", pair.sourceId},
        {"destination_id", pair.destinationId},
        {"source_port", pair.sourcePort},
        {"destination_port", pair.destinationPort},
        {"category", pair.category},
        {"reachable", route != nullptr},
        {"candidate_next_hop_ids", candidates},
        {"selected_next_hop_id",
         route != nullptr ? json(selectedNextHopId) : json(nullptr)},
        {"selected_gateway", AddressString(selectedGateway)},
        {"selected_output_interface", selectedOutputInterface},
        {"candidate_count_before_dedup",
         event != nullptr
           ? event->candidateCountBeforeDedup
           : static_cast<uint32_t>(candidates.size())},
        {"candidate_count_after_dedup",
         event != nullptr
           ? event->candidateCountAfterDedup
           : static_cast<uint32_t>(candidates.size())},
        {"event_selected_gateway",
         event != nullptr
           ? AddressString(event->selectedGateway)
           : AddressString(selectedGateway)},
        {"event_selected_output_interface",
         event != nullptr
           ? event->selectedOutputInterface
           : selectedOutputInterface},
        {"hash_value", event != nullptr ? event->hashValue : 0},
        {"selection_reason",
         event != nullptr ? event->selectionReason : "GLOBAL_FIRST"}
      };
      *output << row.dump() << "\n";
    }
  output->flush();
  NS_ABORT_MSG_IF(!*output, "无法写入 route selection audit");
}

} // namespace

int
main(int argc, char* argv[])
{
  std::string topologyDirectory;
  std::string probePairsFile;
  std::string outputFile;
  std::string auditTimes = "0";
  std::string routingMode = "global-hash-per-flow";
  double simulationDurationSeconds = 1.0;
  uint64_t hashSeed = 1;

  CommandLine commandLine;
  commandLine.AddValue("topologyDir",
                       "Canonical snapshot directory",
                       topologyDirectory);
  commandLine.AddValue("simulationDuration",
                       "Simulation duration in seconds",
                       simulationDurationSeconds);
  commandLine.AddValue("auditTimes",
                       "Comma-separated integer simulation seconds",
                       auditTimes);
  commandLine.AddValue("routingMode",
                       "global-first, global-hash-per-flow, or "
                       "global-hrw-per-flow",
                       routingMode);
  commandLine.AddValue("ecmpHashSeed",
                       "ECMP hash seed",
                       hashSeed);
  commandLine.AddValue("probePairs",
                       "Probe-pairs JSON file",
                       probePairsFile);
  commandLine.AddValue("outputFile",
                       "Destination JSONL file",
                       outputFile);
  commandLine.Parse(argc, argv);

  if (topologyDirectory.empty()
      || probePairsFile.empty()
      || outputFile.empty())
    {
      std::cerr << "topologyDir, probePairs, and outputFile are required"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (routingMode != "global-first"
      && routingMode != "global-hash-per-flow"
      && routingMode != "global-hrw-per-flow")
    {
      std::cerr << "unsupported routingMode: " << routingMode << std::endl;
      return EXIT_FAILURE;
    }
  if (!std::isfinite(simulationDurationSeconds)
      || simulationDurationSeconds <= 0.0)
    {
      std::cerr << "simulationDuration must be finite and positive"
                << std::endl;
      return EXIT_FAILURE;
    }
  std::vector<uint32_t> times =
    ParseRouteAuditTimes(auditTimes, simulationDurationSeconds);
  NS_ABORT_MSG_IF(
    Seconds(times.back()) + NanoSeconds(1)
      >= Seconds(simulationDurationSeconds),
    "simulationDuration 必须晚于最后 audit time 至少 1 ns");
  std::vector<ProbePair> pairs = ReadProbePairs(probePairsFile);
  std::ofstream output(outputFile.c_str(),
                       std::ios::out | std::ios::trunc);
  if (!output)
    {
      std::cerr << "cannot open outputFile: " << outputFile << std::endl;
      return EXIT_FAILURE;
    }

  TopologyConfig config = {
    topologyDirectory,
    simulationDurationSeconds,
    routingMode,
    hashSeed,
    1500,
    1500000,
    false,
    false
  };
  SatelliteTopology topology(config);
  topology.Initialize();
  EcmpRouteRecorder recorder(topology);
  for (uint32_t timeSeconds : times)
    {
      Simulator::Schedule(
        Seconds(timeSeconds) + NanoSeconds(1),
        &WriteSelectionSnapshot,
        &topology,
        &recorder,
        &pairs,
        &output,
        &routingMode,
        timeSeconds);
    }
  Simulator::Stop(Seconds(simulationDurationSeconds));
  Simulator::Run();
  Simulator::Destroy();
  return EXIT_SUCCESS;
}
