/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../support/config-factory.h"
#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace ns3;

namespace
{
void
Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

template <typename F>
void
Reject(F action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, "invalid deadline or input was accepted");
}

class Endpoints : public SatelliteEndpointView
{
  public:
    bool HasSatelliteId(uint32_t id) const override
    {
        return id < 66;
    }

    Ipv4Address GetServiceAddressBySatelliteId(uint32_t id) const override
    {
        return Ipv4Address(0xac100001 + id);
    }
};

void
CheckInputs(const std::filesystem::path& output)
{
    Endpoints endpoints;
    const std::filesystem::path root =
        "contrib/satcompute/input/experiments/leo-66";
    auto profile = ReadComputeProfile(root / "compute/compute-profile.json", endpoints);
    auto trace = ReadTaskTrace(root / "workload/task-trace.json", 1300000000000, endpoints, profile);
    Check(profile.nodes.size() == 66 && trace.tasks.size() == 800, "C800 input counts differ");
    uint64_t input = 0, work = 0, result = 0;
    std::map<TaskProfile, uint64_t> counts;
    for (const auto& node : profile.nodes)
        Check(node.computeRateWorkUnitsPerSecond == 100000, "C800 rate differs");
    for (const auto& task : trace.tasks)
    {
        ++counts[task.taskProfile];
        input += task.inputBytes;
        work += task.computeWorkUnits;
        result += task.outputBytes;
    }
    Check(counts[TaskProfile::DENSE_IMAGE] == 240 && counts[TaskProfile::SPARSE_INFERENCE] == 240 &&
              counts[TaskProfile::COMPRESSION] == 240 && counts[TaskProfile::LLM] == 80,
          "C800 parsed task classes differ");
    Check(input == 193526895311ULL && work == 351623833 && result == 99846517485ULL,
          "C800 parsed ledgers differ");
    nlohmann::json data;
    {
        std::ifstream file(root / "workload/task-trace.json");
        file >> data;
    }
    std::reverse(data["tasks"].begin(), data["tasks"].end());
    auto path = output / "parser.json";
    auto write = [&] {
        std::ofstream file(path);
        file << data;
    };
    write();
    const auto reordered = ReadTaskTrace(path, 1300000000000, endpoints, profile);
    for (size_t i = 0; i < trace.tasks.size(); ++i)
        Check(trace.tasks[i].taskId == reordered.tasks[i].taskId &&
                  trace.tasks[i].taskProfile == reordered.tasks[i].taskProfile,
              "canonical ordering differs");
    for (const auto& bad : {nlohmann::json("unknown"),
                            nlohmann::json(7),
                            nlohmann::json("UNSPECIFIED"),
                            nlohmann::json(nullptr)})
    {
        data["tasks"][0]["task_profile"] = bad;
        write();
        Reject([&] { ReadTaskTrace(path, 1300000000000, endpoints, profile); });
    }
    data["tasks"][0].erase("task_profile");
    write();
    const auto legacy = ReadTaskTrace(path, 1300000000000, endpoints, profile);
    Check(legacy.tasks.back().taskProfile == TaskProfile::UNSPECIFIED,
          "legacy profile was guessed");
}

void
CheckBudget()
{
    Check(CalculateComputeDeadlineBudgetNs(15000000000LL, 1.3) == 19500000000LL,
          "decimal 1.3 introduced an extra nanosecond");
    Check(CalculateComputeDeadlineBudgetNs(1, 1.3) == 2, "deadline did not ceil");
    Check(CalculateComputeDeadlineBudgetNs(std::numeric_limits<int64_t>::max(), 1) ==
              std::numeric_limits<int64_t>::max(),
          "int64 deadline boundary differs");
    for (double bad : {0.99,
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN(),
                       1e300})
        Reject([&] { CalculateComputeDeadlineBudgetNs(1, bad); });
    Reject([] { CalculateComputeDeadlineBudgetNs(std::numeric_limits<int64_t>::max(), 1.3); });
    TaskDefinition definition;
    definition.taskId = 1;
    definition.computeWorkUnits = 1500000;
    TaskRuntime task(definition);
    task.ConfigureComputeDeadline(100000, 1.3);
    Check(task.computeDeadlineTimeNs == -1 && !task.ComputeDeadlineMet(),
          "deadline set before start");
    Check(task.EstablishComputeDeadline(100000000000LL) &&
              task.computeDeadlineTimeNs == 119500000000LL,
          "first compute deadline differs");
    Check(!task.EstablishComputeDeadline(200000000000LL) &&
              task.computeDeadlineTimeNs == 119500000000LL,
          "deadline reset on a later start");
    TaskRuntime overflow(definition);
    overflow.ConfigureComputeDeadline(100000, 1.3);
    Reject([&] { overflow.EstablishComputeDeadline(std::numeric_limits<int64_t>::max()); });
}

struct Outcome
{
    std::vector<TaskRuntime> tasks;
    uint64_t busy{};
};

Outcome
Run(uint64_t rate, double factor, int64_t duration = 200000000)
{
    Outcome outcome;
    {
        auto config =
            satcompute::test::MakeOnlineTestConfig(2, 3, "fixed", duration, duration, 20000000);
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        ComputeProfile profile{{{3, rate}}};
        TaskTrace trace;
        for (uint64_t id : {1, 2})
            trace.tasks.push_back(
                {id, 0, 3, 0, 1024, 1024, 1000, 0, 2 * id - 1, 2 * id, TaskProfile::DENSE_IMAGE});
        auto coordinator = CreateObject<TaskCoordinator>();
        coordinator->Initialize(profile,
                                trace,
                                topology,
                                "size-aware",
                                1024,
                                64028,
                                131072,
                                false,
                                duration,
                                factor);
        Simulator::Stop(NanoSeconds(duration));
        Simulator::Run();
        outcome.tasks = coordinator->GetTaskRuntimes();
        outcome.busy = coordinator->GetComputeServices()[0]->GetBusyTimeNs();
        Check(coordinator->IsComputeAvailable(3), "deadline disabled the satellite");
        coordinator->Dispose();
    }
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
    return outcome;
}

void
CheckExecution()
{
    const auto normal = Run(100000, 1.3);
    const auto boundary = Run(100000, 1.0);
    for (const auto* outcome : {&normal, &boundary})
    {
        Check(outcome->tasks.size() == 2 && outcome->busy == 20000000,
              "normal compute ledger differs");
        for (const auto& task : outcome->tasks)
        {
            Check(task.TaskSucceeded() && task.ComputeDeadlineMet() && task.ResultDelivered(),
                  "on-time compute plus delivered RESULT was not successful");
            Check(task.computeDeadlineTimeNs ==
                      task.computeStartTimeNs + task.computeDeadlineBudgetNs,
                  "deadline depends on arrival or initial queue wait");
            Check(task.resultTransferCompleteTimeNs > task.computeDeadlineTimeNs,
                  "test must exercise RESULT arriving after compute deadline");
        }
        Check(outcome->tasks[1].computeStartTimeNs == outcome->tasks[0].computeCompleteTimeNs,
              "deadline changed non-preemptive FCFS");
    }
    for (const auto& task : boundary.tasks)
        Check(task.computeCompleteTimeNs == task.computeDeadlineTimeNs,
              "same-nanosecond completion did not win over timeout");
    // Slower capacity is test-only; the production C800 ComputeProfile remains homogeneous 100k.
    const auto timeout = Run(50000, 1.3);
    Check(timeout.busy == 26000000, "cancelled work disappeared from busy time");
    for (const auto& task : timeout.tasks)
        Check(task.failureReason == TaskFailureReason::COMPUTE_DEADLINE_EXCEEDED &&
                  task.failureTimeNs == task.computeDeadlineTimeNs &&
                  task.computeCompleteTimeNs == -1 && task.resultTransferStartTimeNs == -1 &&
                  !task.TaskSucceeded(),
              "deadline did not terminate exactly once without RESULT");
    Check(timeout.tasks[1].computeStartTimeNs == timeout.tasks[0].failureTimeNs,
          "deadline did not immediately release FCFS slot");
    const auto truncated = Run(100000, 1.3, normal.tasks[0].computeCompleteTimeNs + 1);
    Check(truncated.tasks[0].ComputeDeadlineMet() && !truncated.tasks[0].TaskSucceeded() &&
              !truncated.tasks[0].ResultDelivered(),
          "unfinished RESULT was counted as success");
}
} // namespace

int
main(int argc, char** argv)
{
    std::string outputDir;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Temporary test directory", outputDir);
    command.Parse(argc, argv);
    try
    {
        Check(!outputDir.empty(), "outputDir is required");
        std::filesystem::create_directories(outputDir);
        CheckInputs(outputDir);
        CheckBudget();
        CheckExecution();
        std::cout << "SatCompute formal-input/deadline tests passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
