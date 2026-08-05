/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/capacity-aware-metrics.h"
#include "ns3/command-line.h"
#include "ns3/ecmp-metrics.h"
#include "ns3/flow-route-registry.h"
#include "ns3/size-aware-metrics.h"

#include "../../third-party/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

using Json = nlohmann::json;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::string
ReadText(const std::filesystem::path& filename)
{
    std::ifstream input(filename, std::ios::binary);
    Check(input.is_open(), "cannot read " + filename.string());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string
FirstLine(const std::filesystem::path& filename)
{
    std::istringstream input(ReadText(filename));
    std::string line;
    std::getline(input, line);
    return line;
}

void
CheckEcmpOutput(const std::filesystem::path& outputDirectory)
{
    EcmpFlowKey flowKey;
    flowKey.sourceAddress = Ipv4Address("172.16.0.1");
    flowKey.destinationAddress = Ipv4Address("172.16.0.4");
    flowKey.protocol = 17;
    flowKey.sourcePort = 10000;
    flowKey.destinationPort = 9000;
    const EcmpRouteDecisionEvent event = {100,
                                          2,
                                          0,
                                          flowKey,
                                          true,
                                          3,
                                          2,
                                          1,
                                          Ipv4Address("10.0.0.6"),
                                          3,
                                          12345,
                                          "HASH_PER_FLOW"};
    WriteEcmpRouteEvents({event}, outputDirectory.string());
    Check(FirstLine(outputDirectory / "ecmp-route-events.csv") ==
              "simulation_time_ns,route_epoch,node_id,source_address,destination_address,"
              "protocol,source_port,destination_port,candidate_count_before_dedup,"
              "candidate_count_after_dedup,selected_index,selected_gateway,"
              "selected_output_interface,hash_value,selection_reason",
          "ECMP metric header differs");
    Check(ReadText(outputDirectory / "ecmp-route-events.csv").find(
              "100,2,0,172.16.0.1,172.16.0.4,17,10000,9000,3,2,1,10.0.0.6,3,12345,"
              "HASH_PER_FLOW") != std::string::npos,
          "ECMP metric row differs");
}

void
CheckSizeAwareOutput(const std::filesystem::path& outputDirectory)
{
    Ptr<FlowRouteRegistry> registry = CreateObject<FlowRouteRegistry>();
    EcmpFlowKey flowKey;
    flowKey.sourceAddress = Ipv4Address("172.16.0.1");
    flowKey.destinationAddress = Ipv4Address("172.16.0.4");
    flowKey.protocol = 17;
    flowKey.sourcePort = 10000;
    flowKey.destinationPort = 9000;
    const EcmpRouteCandidate candidate = {Ipv4Address("10.0.0.2"),
                                          2,
                                          Ipv4Address("172.16.0.4"),
                                          Ipv4Mask("255.255.255.255")};
    registry->RegisterTransfer(flowKey, 7, 4096);
    registry->BeginSending(flowKey);
    registry->RecordAssignment(0, flowKey, candidate, 1, "SIZE_AWARE_HRW_PRIMARY");
    registry->ValidateAssignment(0, flowKey, 1, "SIZE_AWARE_STICKY");
    registry->FinishReceiving(flowKey);

    WriteSizeAwareMetrics(registry, outputDirectory.string());
    Check(FirstLine(outputDirectory / "size-aware-reservation-events.csv") ==
              "simulation_time_ns,action,selection_reason,route_epoch,node_id,transfer_id,"
              "source_address,destination_address,protocol,source_port,destination_port,"
              "declared_bytes,candidate_gateway,candidate_output_interface,"
              "candidate_destination,candidate_destination_mask,candidate_reserved_before,"
              "candidate_reserved_after,total_reserved_before,total_reserved_after",
          "size-aware metric header differs");
    const Json summary = Json::parse(ReadText(outputDirectory / "size-aware-summary.json"));
    Check(summary.at("registered_flow_count") == 1 &&
              summary.at("active_flow_count_at_end") == 0 &&
              summary.at("assignment_count_at_end") == 0 &&
              summary.at("assign_event_count") == 1 &&
              summary.at("sticky_reuse_event_count") == 1 &&
              summary.at("transfer_completed_release_event_count") == 1 &&
              summary.at("peak_total_reserved_bytes") == 4096,
          "size-aware metric summary differs");

    RemoveSizeAwareMetrics(outputDirectory.string());
    Check(!std::filesystem::exists(outputDirectory / "size-aware-reservation-events.csv") &&
              !std::filesystem::exists(outputDirectory / "size-aware-summary.json"),
          "stale size-aware metrics were not removed");
}

void
CheckCapacityAwareOutput(const std::filesystem::path& outputDirectory)
{
    CapacityAwareRuntimeSummary summary;
    summary.activePathCountAtEnd = 2;
    summary.reservedDirectedLinkCountAtEnd = 4;
    summary.totalReservedRateBpsAtEnd = 400000000;
    summary.pendingTransferCountAtEnd = 1;
    WriteCapacityAwareMetrics(summary, outputDirectory.string());
    const Json output = Json::parse(ReadText(outputDirectory / "capacity-aware-summary.json"));
    Check(output.at("active_path_count_at_end") == 2 &&
              output.at("reserved_directed_link_count_at_end") == 4 &&
              output.at("total_reserved_rate_bps_at_end") == 400000000 &&
              output.at("pending_transfer_count_at_end") == 1,
          "capacity-aware metric summary differs");
    RemoveCapacityAwareMetrics(outputDirectory.string());
    Check(!std::filesystem::exists(outputDirectory / "capacity-aware-summary.json"),
          "stale capacity-aware metric was not removed");
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Temporary routing metric output", outputDirectory);
    command.Parse(argc, argv);
    try
    {
        Check(!outputDirectory.empty(), "outputDir is required");
        const std::filesystem::path output =
            std::filesystem::absolute(outputDirectory).lexically_normal();
        CheckEcmpOutput(output);
        CheckSizeAwareOutput(output);
        CheckCapacityAwareOutput(output);
        std::cout << "SatCompute layered routing metric tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
