/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/fault-trace.h"
#include "ns3/satellite-endpoint-view.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

namespace
{

using Json = nlohmann::json;

constexpr int64_t SIMULATION_DURATION_NS = 100;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class FakeEndpointView : public SatelliteEndpointView
{
  public:
    FakeEndpointView()
    {
        for (uint32_t satelliteId = 0; satelliteId < 6; ++satelliteId)
        {
            m_addresses.emplace(
                satelliteId,
                Ipv4Address(("172.16.0." + std::to_string(satelliteId + 1)).c_str()));
        }
    }

    bool
    HasSatelliteId(uint32_t satelliteId) const override
    {
        return m_addresses.contains(satelliteId);
    }

    Ipv4Address
    GetServiceAddressBySatelliteId(uint32_t satelliteId) const override
    {
        return m_addresses.at(satelliteId);
    }

  private:
    std::map<uint32_t, Ipv4Address> m_addresses;
};

Json
MakeFault(uint64_t faultId,
          uint32_t nodeId,
          const std::string& faultType,
          int64_t startTimeNs,
          Json noticeTimeNs,
          Json failureProbability,
          Json durationNs)
{
    return {{"fault_id", faultId},
            {"node_id", nodeId},
            {"fault_type", faultType},
            {"start_time_ns", startTimeNs},
            {"notice_time_ns", std::move(noticeTimeNs)},
            {"failure_probability", std::move(failureProbability)},
            {"duration_ns", std::move(durationNs)}};
}

std::filesystem::path
WriteJson(const std::filesystem::path& directory,
          const std::string& filename,
          const Json& contents)
{
    const std::filesystem::path path = directory / filename;
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    Check(output.is_open(), "cannot create fault-trace test input");
    output << contents.dump(2) << '\n';
    return path;
}

std::filesystem::path
WriteText(const std::filesystem::path& directory,
          const std::string& filename,
          const std::string& contents)
{
    const std::filesystem::path path = directory / filename;
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    Check(output.is_open(), "cannot create malformed fault-trace test input");
    output << contents;
    return path;
}

bool
SameFault(const FaultDefinition& left, const FaultDefinition& right)
{
    return left.faultId == right.faultId && left.nodeId == right.nodeId &&
           left.faultType == right.faultType && left.startTimeNs == right.startTimeNs &&
           left.noticeTimeNs == right.noticeTimeNs &&
           left.failureProbability == right.failureProbability &&
           left.durationNs == right.durationNs;
}

void
ExpectError(const std::filesystem::path& directory,
            const std::string& name,
            const Json& root,
            const FakeEndpointView& endpoints,
            const ComputeProfile* profile,
            const std::string& expectedField)
{
    const std::filesystem::path path = WriteJson(directory, name + ".json", root);
    try
    {
        ReadFaultTrace(path, SIMULATION_DURATION_NS, endpoints, profile);
    }
    catch (const FaultTraceError& error)
    {
        const std::string message = error.what();
        Check(message.find(std::filesystem::absolute(path).lexically_normal().string()) !=
                      std::string::npos &&
                  message.find(expectedField) != std::string::npos,
              "fault-trace error omitted path or field: " + message);
        return;
    }
    throw std::runtime_error("invalid fault trace was accepted: " + name);
}

void
CheckValidAndCanonical(const std::filesystem::path& directory,
                       const FakeEndpointView& endpoints,
                       const ComputeProfile& profile)
{
    const Json first = {
        {"faults",
         {MakeFault(3, 5, "satellite", 50, nullptr, nullptr, nullptr),
          MakeFault(1, 1, "compute", 20, 10, 0.82, 5),
          MakeFault(2, 2, "satellite", 25, 25, 0.0, 100)}}};
    const Json second = {
        {"faults",
         {MakeFault(2, 2, "satellite", 25, 25, 0.0, 100),
          MakeFault(3, 5, "satellite", 50, nullptr, nullptr, nullptr),
          MakeFault(1, 1, "compute", 20, 10, 0.82, 5)}}};
    const FaultTrace firstTrace = ReadFaultTrace(WriteJson(directory, "valid-a.json", first),
                                                 SIMULATION_DURATION_NS,
                                                 endpoints,
                                                 &profile);
    const FaultTrace secondTrace = ReadFaultTrace(WriteJson(directory, "valid-b.json", second),
                                                  SIMULATION_DURATION_NS,
                                                  endpoints,
                                                  &profile);
    Check(firstTrace.sourcePath.is_absolute() && firstTrace.faults.size() == 3 &&
              secondTrace.faults.size() == 3,
          "valid fault trace source or count differs");
    for (std::size_t index = 0; index < firstTrace.faults.size(); ++index)
    {
        Check(SameFault(firstTrace.faults[index], secondTrace.faults[index]),
              "fault input array order changed canonical trace");
        Check(firstTrace.faults[index].faultId == index + 1,
              "fault trace was not sorted by fault_id");
    }
    const FaultDefinition& compute = firstTrace.faults.front();
    Check(compute.faultType == FaultType::COMPUTE &&
              std::string(FaultTypeToString(compute.faultType)) == "compute" &&
              compute.GetRecoveryTimeNs() == 25 &&
              compute.GetWarningLeadTimeNs() == 10,
          "derived compute fault times or type differ");
    Check(!firstTrace.faults.back().GetRecoveryTimeNs().has_value() &&
              !firstTrace.faults.back().GetWarningLeadTimeNs().has_value(),
          "permanent sudden fault derived finite metadata");

    const Json adjacent = {
        {"faults",
         {MakeFault(10, 4, "satellite", 10, nullptr, nullptr, 5),
          MakeFault(11, 4, "satellite", 15, nullptr, nullptr, nullptr)}}};
    Check(ReadFaultTrace(WriteJson(directory, "adjacent.json", adjacent),
                         SIMULATION_DURATION_NS,
                         endpoints,
                         &profile)
                  .faults.size() == 2,
          "fault beginning at the exact recovery time was rejected");

    const Json empty = {{"faults", Json::array()}};
    Check(ReadFaultTrace(WriteJson(directory, "empty.json", empty),
                         SIMULATION_DURATION_NS,
                         endpoints,
                         &profile)
                  .faults.empty(),
          "explicit empty fault trace was rejected");
}

void
CheckInvalidInputs(const std::filesystem::path& directory,
                   const FakeEndpointView& endpoints,
                   const ComputeProfile& profile)
{
    const auto valid = [] {
        return MakeFault(1, 1, "compute", 20, 10, 0.5, 5);
    };
    const auto root = [](const Json& fault) {
        return Json{{"faults", Json::array({fault})}};
    };

    Json value = {{"faults", Json::array()}, {"unknown", true}};
    ExpectError(directory, "unknown-root", value, endpoints, &profile, "root");
    ExpectError(directory,
                "faults-not-array",
                Json{{"faults", Json::object()}},
                endpoints,
                &profile,
                "faults");

    value = valid();
    value.erase("duration_ns");
    ExpectError(directory, "missing-field", root(value), endpoints, &profile, "faults[0]");
    value = valid();
    value["unknown"] = true;
    ExpectError(directory, "unknown-field", root(value), endpoints, &profile, "faults[0]");

    value = valid();
    value["fault_id"] = 0;
    ExpectError(directory, "zero-id", root(value), endpoints, &profile, "fault_id");
    value = {{"faults", {valid(), valid()}}};
    ExpectError(directory, "duplicate-id", value, endpoints, &profile, "fault_id");

    value = valid();
    value["node_id"] = 6;
    ExpectError(directory, "unknown-node", root(value), endpoints, &profile, "faults[0]");
    value = valid();
    value["node_id"] = 2;
    ExpectError(directory, "compute-node", root(value), endpoints, &profile, "ComputeProfile");
    ExpectError(directory, "missing-profile", root(valid()), endpoints, nullptr, "ComputeProfile");

    value = valid();
    value["fault_type"] = "communication";
    ExpectError(directory, "fault-type", root(value), endpoints, &profile, "fault_type");
    value = valid();
    value["start_time_ns"] = SIMULATION_DURATION_NS;
    ExpectError(directory, "late-start", root(value), endpoints, &profile, "start_time_ns");
    value = valid();
    value["start_time_ns"] = -1;
    ExpectError(directory, "negative-start", root(value), endpoints, &profile, "start_time_ns");
    value = valid();
    value["start_time_ns"] = 1.5;
    ExpectError(directory, "fractional-start", root(value), endpoints, &profile, "start_time_ns");

    value = valid();
    value["failure_probability"] = nullptr;
    ExpectError(directory, "notice-only", root(value), endpoints, &profile, "faults[0]");
    value = valid();
    value["notice_time_ns"] = nullptr;
    ExpectError(directory, "probability-only", root(value), endpoints, &profile, "faults[0]");
    value = valid();
    value["notice_time_ns"] = 21;
    ExpectError(directory, "late-notice", root(value), endpoints, &profile, "notice_time_ns");
    for (const auto& [name, probability] :
         std::vector<std::pair<std::string, Json>>{{"low-probability", -0.1},
                                                   {"high-probability", 1.1},
                                                   {"text-probability", "likely"}})
    {
        value = valid();
        value["failure_probability"] = probability;
        ExpectError(directory, name, root(value), endpoints, &profile, "failure_probability");
    }

    for (const auto& [name, duration] :
         std::vector<std::pair<std::string, Json>>{{"zero-duration", 0},
                                                   {"negative-duration", -1},
                                                   {"fractional-duration", 1.5}})
    {
        value = valid();
        value["duration_ns"] = duration;
        ExpectError(directory, name, root(value), endpoints, &profile, "duration_ns");
    }
    value = valid();
    value["start_time_ns"] = 99;
    value["notice_time_ns"] = nullptr;
    value["failure_probability"] = nullptr;
    value["duration_ns"] = std::numeric_limits<int64_t>::max();
    ExpectError(directory, "recovery-overflow", root(value), endpoints, &profile, "duration_ns");

    const Json overlap = {
        {"faults",
         {MakeFault(1, 1, "compute", 10, nullptr, nullptr, 10),
          MakeFault(2, 1, "satellite", 19, nullptr, nullptr, 5)}}};
    ExpectError(directory, "overlap", overlap, endpoints, &profile, "overlapping");

    const std::filesystem::path malformed =
        WriteText(directory, "malformed.json", "{\"faults\":[");
    try
    {
        ReadFaultTrace(malformed, SIMULATION_DURATION_NS, endpoints, &profile);
    }
    catch (const FaultTraceError& error)
    {
        Check(std::string(error.what()).find("JSON") != std::string::npos,
              "malformed JSON error category differs");
        return;
    }
    throw std::runtime_error("malformed fault JSON was accepted");
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Temporary fault-trace test directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!outputDirectory.empty(), "outputDir is required");
        std::filesystem::create_directories(outputDirectory);
        const FakeEndpointView endpoints;
        ComputeProfile profile;
        profile.nodes = {{1, 1000000000}, {3, 2000000000}};
        CheckValidAndCanonical(outputDirectory, endpoints, profile);
        CheckInvalidInputs(outputDirectory, endpoints, profile);
        std::cout << "SatCompute fault trace tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
