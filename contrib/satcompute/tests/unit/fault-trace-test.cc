/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/command-line.h"
#include "ns3/fault-trace.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace ns3;
namespace
{
void
Check(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error(message);
}

void
CheckWriter(const std::filesystem::path& directory)
{
    FaultDefinition compute;
    compute.faultId = 1;
    compute.nodeId = 1;
    compute.startTimeNs = 20;
    compute.failureProbability = 0.5;
    compute.durationNs = 5;
    compute.pF1 = .5;
    compute.pF2 = 0;
    compute.f1Occurred = true;
    FaultDefinition satellite;
    satellite.faultId = 2;
    satellite.nodeId = 5;
    satellite.faultType = FaultType::SATELLITE;
    satellite.startTimeNs = 60;
    FaultTrace trace;
    trace.faults = {satellite, compute};
    const auto first = directory / "written-v2-a.json";
    const auto second = directory / "written-v2-b.json";
    WriteFaultTraceV2(first, trace);
    std::reverse(trace.faults.begin(), trace.faults.end());
    WriteFaultTraceV2(second, trace);
    std::ifstream a(first), b(second);
    const std::string bytesA((std::istreambuf_iterator<char>(a)), {});
    const std::string bytesB((std::istreambuf_iterator<char>(b)), {});
    Check(bytesA == bytesB, "canonical output depends on input ordering");
    const auto read = ReadValidationFaultTrace(first, {1, 5}, 100);
    WriteFaultTraceV2(directory / "validation-roundtrip.json", read);
    std::ifstream roundtrip(directory / "validation-roundtrip.json");
    Check(std::string((std::istreambuf_iterator<char>(roundtrip)), {}) == bytesA,
          "frozen evidence roundtrip differs");
    for (const auto& nodes : {std::vector<uint32_t>{1}, std::vector<uint32_t>{5}})
    {
        bool rejected = false;
        try
        {
            ReadValidationFaultTrace(first, nodes, 100);
        }
        catch (const FaultTraceError&)
        {
            rejected = true;
        }
        Check(rejected, "validation accepted unknown satellite");
    }
    bool outside = false;
    try
    {
        ReadValidationFaultTrace(first, {1, 5}, 60);
    }
    catch (const FaultTraceError&)
    {
        outside = true;
    }
    Check(outside, "validation accepted START at simulation endpoint");
    const auto json = nlohmann::json::parse(bytesA);
    Check(json.at("schema_version") == 2 && json.at("faults").size() == 2, "writer root differs");
    const auto& entries = json.at("faults");
    for (std::size_t i = 0; i < entries.size(); ++i)
        Check(entries[i].at("fault_id") == i + 1 && entries[i].size() == 13,
              "canonical order or output fields differ");
    Check(entries[0].at("failure_probability") == .5 &&
          entries[1].at("duration_ns").is_null() &&
          !entries[0].contains("notice_time_ns") && !entries[0].contains("risk_duration_ns"),
          "writer must contain only START metadata");
    auto invalid = [&](const std::function<void(FaultTrace&)>& mutate) {
        auto candidate = trace;
        mutate(candidate);
        try
        {
            WriteFaultTraceV2(directory / "invalid.json", candidate);
        }
        catch (const FaultTraceError&)
        {
            return;
        }
        throw std::runtime_error("invalid generated trace accepted");
    };
    invalid([](auto& t) { t.schemaVersion = 1; });
    invalid([](auto& t) { t.faults[0].faultId = 0; });
    invalid([](auto& t) { t.faults.push_back(t.faults[0]); });
    invalid([](auto& t) { t.faults[0].failureProbability = 1.1; });
    invalid(
        [](auto& t) { t.faults[0].failureProbability = std::numeric_limits<double>::quiet_NaN(); });
    invalid([](auto& t) { t.faults[0].durationNs.reset(); });
    invalid([](auto& t) { t.faults[0].durationNs = 0; });
    invalid([](auto& t) { t.faults[0].startTimeNs = -1; });
    invalid([](auto& t) { t.faults[0].durationNs = std::numeric_limits<int64_t>::max(); });
    invalid([](auto& t) { t.faults[0].faultOccurred = false; });
    invalid([](auto& t) { t.faults[1].durationNs = 5; });
    invalid([](auto& t) {
        auto f = t.faults[0];
        f.faultId = 4;
        t.faults.push_back(f);
    });
    WriteFaultTraceV2(directory / "empty.json", FaultTrace{});
}
}

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Temporary writer test directory", outputDirectory);
    command.Parse(argc, argv);
    try
    {
        Check(!outputDirectory.empty(), "outputDir required");
        CheckWriter(outputDirectory);
        std::cout << "SatCompute fault trace writer tests passed.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
