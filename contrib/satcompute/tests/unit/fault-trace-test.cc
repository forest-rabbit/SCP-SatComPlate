/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/fault-trace.h"
#include "ns3/satellite-endpoint-view.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

Json
MakeV2Fault(uint64_t faultId,
            uint32_t nodeId,
            const std::string& faultType,
            bool faultOccurred,
            Json noticeTimeNs,
            Json startTimeNs,
            Json failureProbability,
            Json warningLeadTimeNs,
            Json riskDurationNs,
            Json durationNs)
{
    return {{"fault_id", faultId},
            {"node_id", nodeId},
            {"fault_type", faultType},
            {"fault_occurred", faultOccurred},
            {"notice_time_ns", std::move(noticeTimeNs)},
            {"start_time_ns", std::move(startTimeNs)},
            {"failure_probability", std::move(failureProbability)},
            {"warning_lead_time_ns", std::move(warningLeadTimeNs)},
            {"risk_duration_ns", std::move(riskDurationNs)},
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
           left.faultType == right.faultType &&
           left.faultOccurred == right.faultOccurred &&
           left.startTimeNs == right.startTimeNs &&
           left.noticeTimeNs == right.noticeTimeNs &&
           left.failureProbability == right.failureProbability &&
           left.warningLeadTimeNs == right.warningLeadTimeNs &&
           left.riskDurationNs == right.riskDurationNs &&
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
    Check(firstTrace.schemaVersion == 1 && firstTrace.sourcePath.is_absolute() &&
              firstTrace.faults.size() == 3 && secondTrace.faults.size() == 3,
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
              compute.GetWarningLeadTimeNs() == 10 &&
              compute.warningLeadTimeNs == 10,
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

void
CheckV2RoundTrip(const std::filesystem::path& directory,
                 const FakeEndpointView& endpoints,
                 const ComputeProfile& profile)
{
    const Json root = {
        {"schema_version", 2},
        {"faults",
         {MakeV2Fault(4,
                      5,
                      "satellite",
                      true,
                      nullptr,
                      60,
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr),
          MakeV2Fault(2,
                      3,
                      "compute",
                      false,
                      30,
                      nullptr,
                      0.2,
                      nullptr,
                      10,
                      nullptr),
          MakeV2Fault(1, 1, "compute", true, 10, 20, 0.1, 10, nullptr, 5),
          MakeV2Fault(3,
                      1,
                      "compute",
                      true,
                      nullptr,
                      50,
                      0.3,
                      nullptr,
                      nullptr,
                      5)}}};
    const FaultTrace trace = ReadFaultTrace(WriteJson(directory, "valid-v2.json", root),
                                            SIMULATION_DURATION_NS,
                                            endpoints,
                                            &profile);
    Check(trace.schemaVersion == 2 && trace.faults.size() == 4,
          "valid v2 trace schema or count differs");
    const FaultDefinition& warned = trace.faults[0];
    const FaultDefinition& riskOnly = trace.faults[1];
    Check(warned.warningLeadTimeNs == 10 && warned.GetWarningLeadTimeNs() == 10,
          "warned compute lead time differs");
    Check(!riskOnly.faultOccurred && !riskOnly.startTimeNs.has_value() &&
              riskOnly.riskDurationNs == 10 && riskOnly.GetRiskClearTimeNs() == 40,
          "risk-only episode fields differ");

    const std::filesystem::path firstOutput = directory / "written-v2-a.json";
    const std::filesystem::path secondOutput = directory / "written-v2-b.json";
    FaultTrace shuffled = trace;
    std::reverse(shuffled.faults.begin(), shuffled.faults.end());
    WriteFaultTraceV2(firstOutput, shuffled);
    WriteFaultTraceV2(secondOutput, shuffled);
    std::ifstream first(firstOutput);
    std::ifstream second(secondOutput);
    const std::string firstBytes((std::istreambuf_iterator<char>(first)),
                                 std::istreambuf_iterator<char>());
    const std::string secondBytes((std::istreambuf_iterator<char>(second)),
                                  std::istreambuf_iterator<char>());
    Check(firstBytes == secondBytes, "v2 writer output is not byte deterministic");
    const Json written = Json::parse(firstBytes);
    for (std::size_t index = 0; index < written.at("faults").size(); ++index)
    {
        Check(written.at("faults").at(index).at("fault_id") == index + 1,
              "v2 writer did not use canonical anchor ordering");
    }
    const FaultTrace roundTrip =
        ReadFaultTrace(firstOutput, SIMULATION_DURATION_NS, endpoints, &profile);
    Check(roundTrip.faults.size() == trace.faults.size(),
          "v2 writer round-trip count differs");
    for (std::size_t index = 0; index < trace.faults.size(); ++index)
    {
        Check(SameFault(roundTrip.faults[index], trace.faults[index]),
              "v2 writer round-trip fields differ");
    }
}

void
CheckInvalidV2(const std::filesystem::path& directory,
               const FakeEndpointView& endpoints,
               const ComputeProfile& profile)
{
    const auto valid = [] {
        return MakeV2Fault(1, 1, "compute", true, 10, 20, 0.5, 10, nullptr, 5);
    };
    const auto root = [](const Json& fault) {
        return Json{{"schema_version", 2}, {"faults", Json::array({fault})}};
    };

    ExpectError(directory,
                "v2-unsupported-schema",
                Json{{"schema_version", 3}, {"faults", Json::array()}},
                endpoints,
                &profile,
                "schema_version");
    Json value = valid();
    value.erase("risk_duration_ns");
    ExpectError(directory, "v2-missing-field", root(value), endpoints, &profile, "faults[0]");
    value = valid();
    value["unknown"] = true;
    ExpectError(directory, "v2-unknown-field", root(value), endpoints, &profile, "faults[0]");
    value = valid();
    value["fault_occurred"] = "yes";
    ExpectError(directory, "v2-non-bool-occurred", root(value), endpoints, &profile, "fault_occurred");
    value = valid();
    value["start_time_ns"] = nullptr;
    ExpectError(directory, "v2-occurred-without-start", root(value), endpoints, &profile, "start_time_ns");
    value = valid();
    value["failure_probability"] = nullptr;
    ExpectError(directory, "v2-compute-without-probability", root(value), endpoints, &profile, "failure_probability");
    value = valid();
    value["duration_ns"] = nullptr;
    ExpectError(directory, "v2-compute-without-duration", root(value), endpoints, &profile, "duration_ns");
    value = valid();
    value["risk_duration_ns"] = 1;
    ExpectError(directory, "v2-occurred-with-risk-duration", root(value), endpoints, &profile, "risk_duration_ns");
    value = valid();
    value["fault_occurred"] = false;
    ExpectError(directory, "v2-false-with-start", root(value), endpoints, &profile, "start_time_ns");
    value = valid();
    value["warning_lead_time_ns"] = 9;
    ExpectError(directory, "v2-lead-mismatch", root(value), endpoints, &profile, "warning_lead_time_ns");
    value = MakeV2Fault(1, 1, "compute", true, nullptr, 20, 0.5, 1, nullptr, 5);
    ExpectError(directory, "v2-lead-without-notice", root(value), endpoints, &profile, "warning_lead_time_ns");
    value = MakeV2Fault(1, 1, "compute", false, nullptr, nullptr, 0.5, nullptr, 10, nullptr);
    ExpectError(directory, "v2-risk-without-notice", root(value), endpoints, &profile, "notice_time_ns");
    value = MakeV2Fault(1, 1, "compute", false, 20, nullptr, nullptr, nullptr, 10, nullptr);
    ExpectError(directory, "v2-risk-without-probability", root(value), endpoints, &profile, "failure_probability");
    value = MakeV2Fault(1, 1, "compute", false, 20, nullptr, 0.5, 1, 10, nullptr);
    ExpectError(directory, "v2-risk-with-lead", root(value), endpoints, &profile, "warning_lead_time_ns");
    value = MakeV2Fault(1, 1, "compute", false, 20, nullptr, 0.5, nullptr, nullptr, nullptr);
    ExpectError(directory, "v2-risk-without-duration", root(value), endpoints, &profile, "risk_duration_ns");
    value = MakeV2Fault(1, 1, "compute", false, 20, nullptr, 0.5, nullptr, 10, 5);
    ExpectError(directory, "v2-risk-with-recovery", root(value), endpoints, &profile, "duration_ns");
    value = MakeV2Fault(1, 1, "compute", false, 90, nullptr, 0.5, nullptr, 20, nullptr);
    ExpectError(directory, "v2-risk-past-end", root(value), endpoints, &profile, "risk_duration_ns");
    value = MakeV2Fault(1, 5, "satellite", true, nullptr, 20, 0.1, nullptr, nullptr, nullptr);
    ExpectError(directory, "v2-satellite-probability", root(value), endpoints, &profile, "failure_probability");
    value = MakeV2Fault(1, 5, "satellite", false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    ExpectError(directory, "v2-satellite-not-occurred", root(value), endpoints, &profile, "fault_occurred");
    value = MakeV2Fault(1, 5, "satellite", true, 10, 20, nullptr, nullptr, nullptr, nullptr);
    ExpectError(directory, "v2-satellite-notice", root(value), endpoints, &profile, "notice_time_ns");
    value = MakeV2Fault(1, 5, "satellite", true, nullptr, 20, nullptr, nullptr, nullptr, 5);
    ExpectError(directory, "v2-satellite-recovery", root(value), endpoints, &profile, "duration_ns");
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
        CheckV2RoundTrip(outputDirectory, endpoints, profile);
        CheckInvalidV2(outputDirectory, endpoints, profile);
        std::cout << "SatCompute fault trace tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
