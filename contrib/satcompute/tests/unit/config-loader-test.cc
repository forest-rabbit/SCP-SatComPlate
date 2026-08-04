/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/scenario-config.h"
#include "ns3/sha256.h"

#include "../../third-party/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void
ExpectConfigError(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const ScenarioConfigError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

std::string
ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("cannot open test output: " + path.string());
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void
WriteJson(const std::filesystem::path& path, const nlohmann::json& payload)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot create invalid test input: " + path.string());
    }
    output << payload.dump(2) << '\n';
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string fixedScenario;
    std::string distanceScenario;
    std::string taskScenario;
    std::string invalidScenario;
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("fixedScenario", "Fixed-delay scenario fixture", fixedScenario);
    command.AddValue("distanceScenario", "Distance-delay scenario fixture", distanceScenario);
    command.AddValue("taskScenario", "Task-input scenario fixture", taskScenario);
    command.AddValue("invalidScenario", "Invalid scenario fixture", invalidScenario);
    command.AddValue("outputDir", "Temporary test output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!fixedScenario.empty(), "fixedScenario is required");
        Check(!distanceScenario.empty(), "distanceScenario is required");
        Check(!taskScenario.empty(), "taskScenario is required");
        Check(!invalidScenario.empty(), "invalidScenario is required");
        Check(!outputDirectory.empty(), "outputDir is required");

        Check(Sha256Bytes("") ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA-256 empty-string vector differs");
        Check(Sha256Bytes("abc") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 abc vector differs");

        Check(ParseSecondsToNanoseconds("20", "time") == 20000000000,
              "integer-second conversion differs");
        Check(ParseSecondsToNanoseconds("1.000000001", "time") == 1000000001,
              "fractional-second conversion differs");
        Check(ParseSecondsToNanoseconds("2e-9", "time") == 2,
              "exponent-second conversion differs");
        ExpectConfigError(
            [] { ParseSecondsToNanoseconds("0.0000000001", "time"); },
            "sub-nanosecond precision was accepted");
        ExpectConfigError(
            [] { ParseSecondsToNanoseconds("0", "time", true); },
            "zero positive duration was accepted");
        ExpectConfigError(
            [] { ParseSecondsToNanoseconds("9223372036.854775808", "time"); },
            "overflowing duration was accepted");

        const ScenarioConfig fixed = LoadScenarioConfig(fixedScenario);
        Check(Sha256File(fixed.sourcePath) ==
                  "82ccd25b93e113669dd133d9fa1d1d2dcc95de81f71e690090496c3e7be51470",
              "streaming scenario SHA-256 differs");
        Check(fixed.constellation.GetSatelliteCount() == 66, "fixed satellite count differs");
        Check(fixed.simulation.durationNs == 1000000000000,
              "fixed simulation duration differs");
        Check(fixed.network.networkUpdateIntervalNs == 20000000000,
              "fixed network update interval differs");
        Check(fixed.network.fixedDelayUs == 8000, "fixed delay differs");
        Check(fixed.traceExport.intervalNs == 1000000000, "trace interval differs");

        const ScenarioConfig distance = LoadScenarioConfig(distanceScenario);
        Check(distance.network.delayMode == "distance", "distance mode differs");
        Check(!distance.network.fixedDelayUs, "distance fixed delay is not null");
        Check(distance.network.networkUpdateIntervalNs == 1000000000,
              "distance network update interval differs");

        const ScenarioConfig task = LoadScenarioConfig(taskScenario);
        Check(task.workloads.computeProfile.has_value(), "compute profile was not resolved");
        Check(task.workloads.taskTrace.has_value(), "task trace was not resolved");
        Check(task.workloads.computeProfile->is_absolute(), "compute profile is not absolute");
        Check(task.workloads.taskTrace->is_absolute(), "task trace is not absolute");

        ExpectConfigError(
            [&invalidScenario] { LoadScenarioConfig(invalidScenario); },
            "unknown root field was accepted");

        nlohmann::json invalidNested =
            nlohmann::json::parse(ReadFile(fixed.sourcePath));
        invalidNested.at("network")["unexpected"] = true;
        const std::filesystem::path invalidNestedPath =
            std::filesystem::path(outputDirectory) / "invalid-nested.json";
        WriteJson(invalidNestedPath, invalidNested);
        ExpectConfigError(
            [&invalidNestedPath] { LoadScenarioConfig(invalidNestedPath); },
            "unknown nested field was accepted");

        nlohmann::json invalidPrecision =
            nlohmann::json::parse(ReadFile(fixed.sourcePath));
        invalidPrecision.at("simulation")["duration_s"] = 0.0000000001;
        const std::filesystem::path invalidPrecisionPath =
            std::filesystem::path(outputDirectory) / "invalid-precision.json";
        WriteJson(invalidPrecisionPath, invalidPrecision);
        ExpectConfigError(
            [&invalidPrecisionPath] { LoadScenarioConfig(invalidPrecisionPath); },
            "sub-nanosecond scenario time was accepted");

        nlohmann::json invalidQueue = nlohmann::json::parse(ReadFile(fixed.sourcePath));
        invalidQueue.at("network")["isl_queue_bytes"] = 4294967296ULL;
        const std::filesystem::path invalidQueuePath =
            std::filesystem::path(outputDirectory) / "invalid-queue.json";
        WriteJson(invalidQueuePath, invalidQueue);
        ExpectConfigError(
            [&invalidQueuePath] { LoadScenarioConfig(invalidQueuePath); },
            "ISL queue larger than ns-3 QueueSize was accepted");

        const std::filesystem::path effectivePath =
            WriteEffectiveConfig(fixed, outputDirectory);
        const std::string firstOutput = ReadFile(effectivePath);
        const std::string secondOutput =
            ReadFile(WriteEffectiveConfig(fixed, outputDirectory));
        Check(firstOutput == secondOutput, "effective configuration is not deterministic");

        const nlohmann::json effective = nlohmann::json::parse(firstOutput);
        Check(effective.at("simulation").at("duration_ns") == 1000000000000,
              "effective duration differs");
        Check(effective.at("network").at("network_update_interval_ns") == 20000000000,
              "effective network interval differs");
        Check(effective.at("input_hashes").at("scenario_config").at("sha256") ==
                  Sha256File(fixed.sourcePath),
              "effective scenario hash differs");

        const nlohmann::json taskEffective = nlohmann::json::parse(
            ReadFile(WriteEffectiveConfig(task, outputDirectory)));
        Check(taskEffective.at("input_hashes").at("compute_profile").at("sha256") ==
                  Sha256File(*task.workloads.computeProfile),
              "effective compute profile hash differs");
        Check(taskEffective.at("input_hashes").at("task_trace").at("sha256") ==
                  Sha256File(*task.workloads.taskTrace),
              "effective task trace hash differs");

        std::cout << "SatCompute C++ configuration tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
