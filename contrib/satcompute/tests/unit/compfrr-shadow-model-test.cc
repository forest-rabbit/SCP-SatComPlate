/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/command-line.h"
#include "ns3/compfrr-shadow-model.h"
#include "ns3/compfrr-shadow-task-state.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ns3;
using namespace ns3::compfrr;

namespace
{
void
Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void
Near(double actual, double expected, const char* message)
{
    Check(std::abs(actual - expected) <= 1e-10 * std::max(1.0, std::abs(expected)), message);
}

template <typename Function>
void
Reject(Function f)
{
    try
    {
        f();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("invalid shadow input accepted");
}

void
PureChecks()
{
    Check(CostTier(0).tier == 1 && CostTier(100000000).tier == 1, "tier1 inclusive boundary");
    Check(CostTier(100000001).tier == 2 && CostTier(500000000).tier == 2,
          "tier2 inclusive boundary");
    Check(CostTier(500000001).tier == 3, "tier3 boundary");
    Near(CostTier(100000000).local, .0001, "cL tier1");
    Near(CostTier(500000000).remote, .002, "cR tier2");

    DecisionInput in{1000000000,
                     1500000,
                     500000000,
                     100000,
                     1250000000,
                     .6,
                     6,
                     4.5,
                     .2,
                     .8,
                     CostTier(500000000)};
    Near(RecomputeCatchUp(in), 9.8, "OFF = .8 input wait + 9 redo");
    const auto off = EstimateCatchUp(in, false, 0, 0);
    Near(off.executedWasteWu, 900000, "OFF redo WU counted once");
    Near(off.idleWasteWu, 80000, "OFF input idle WU");
    Near(off.seconds, 9.8, "OFF catch-up");
    const auto on = EstimateCatchUp(in, true, .5, .4);
    Near(on.seconds, .04 + .002 + 1.5, "ON tail transfer + merge + redo");
    Near(on.executedWasteWu, 150200, "ON extra executed work");
    Near(on.idleWasteWu, 4000, "ON reserved idle work");
    Near(EstimateCatchUp(in, true, .5, .5).seconds, 1.5, "l=r omits cR");
    Near(EstimateCatchUp(in, true, .6, .6).seconds, 0, "fully protected current state");
    Reject([&] { EstimateCatchUp(in, true, .5, .6); });
    Reject([&] { EstimateCatchUp(in, true, .7, .6); });
    Near(InitializationTime(in, 300000000), .802, "parallel input/state initialization");
    auto choice = SelectFrequency(in, false);
    Check(choice.best && choice.feasibleCount > 0, "enumeration finds candidate");
    Check(choice.best->deltaPermille >= 10 && choice.best->deltaPermille <= 100 &&
              choice.best->deltaPermille * choice.best->remoteEvery <= 1000,
          "legal frequency");
    Check(ShouldStartProtection(in, choice, .802), "beneficial START");
    Check(!ShouldStartProtection(in, choice, 6), "initialization strict remaining-time boundary");
    // Build exactly representable zero tie independently of floating cancellation.
    auto zero = in;
    zero.costs = {0, 0, 1};
    zero.pFinish = 0;
    zero.qOneSecond = 0;
    auto tied = SelectFrequency(zero, true);
    Check(tied.best->deltaPermille == 10 && tied.best->remoteEvery == 1,
          "exact ties use lexicographic delta then n");
    Check(!ShouldStartProtection(zero, tied, .1), "START exact tie stays OFF");
    in.pFinish = 0;
    Check(!ShouldStartProtection(in, SelectFrequency(in, false), .1), "zero P cannot START");
    auto q0 = in;
    q0.qOneSecond = 0;
    auto low = SelectFrequency(q0, true);
    Check(low.best->deltaPermille == 100 && low.best->remoteEvery == 10,
          "q=0 minimizes maintenance with sparse frequency");
    auto invalid = in;
    invalid.deadlineSlack = -1;
    Check(!SelectFrequency(invalid, false).best, "negative slack is infeasible, not clamped");
    invalid.deadlineSlack = 0;
    Check(!SelectFrequency(invalid, true).best, "zero slack empty candidate set");
    invalid = in;
    invalid.nodeAvailable = false;
    Check(!SelectFrequency(invalid, true).best, "unavailable candidate node");
    invalid = in;
    invalid.pathAvailable = false;
    Check(!SelectFrequency(invalid, false).best, "unavailable candidate path");
    invalid = in;
    invalid.progress = 0;
    Near(RecomputeCatchUp(invalid), .8, "x=0 recompute only input");
    invalid.progress = .999999;
    invalid.remainingSeconds = .000015;
    Check(!ShouldStartProtection(invalid, choice, .8), "near finish initialization infeasible");
    invalid.rate = 0;
    Reject([&] { SelectFrequency(invalid, true); });
    invalid = in;
    invalid.qOneSecond = std::numeric_limits<double>::quiet_NaN();
    Reject([&] { SelectFrequency(invalid, true); });

    TaskDefinition dense;
    dense.taskProfile = TaskProfile::DENSE_IMAGE;
    dense.inputBytes = 100000000;
    dense.computeWorkUnits = 150000;
    auto layout = MakeWorkloadLayout(dense);
    Check(layout.variableBytes == 100000762, "dense variable index included in K tier2");
    Check(CostTier(layout.variableBytes).tier == 2, "tier uses K, not INPUT");
    Check(layout.Floor(0) == 0 && layout.Floor(layout.work) == layout.work,
          "legal floor endpoints");
    Check(!layout.Next(layout.work, layout.work), "no duplicate final checkpoint");
    uint64_t sum = 0, previous = 0;
    for (auto work : layout.boundaries)
    {
        sum += layout.StateAt(work) - layout.StateAt(previous);
        previous = work;
    }
    Check(sum == layout.variableBytes, "legal increments conserve exact variable bytes");
    Check(*layout.Next(1, 0) == 787, "G1 524288-byte tile maps to ceil WU");
    dense.computeWorkUnits++;
    Reject([&] { MakeWorkloadLayout(dense); });
    TaskDefinition llm;
    llm.taskProfile = TaskProfile::LLM;
    llm.inputBytes = 400;
    llm.computeWorkUnits = 500000;
    const auto tokens = MakeWorkloadLayout(llm);
    Check(tokens.variableBytes == 573440000 && tokens.Floor(199) == 100,
          "LLM whole-token boundary");
    Check(tokens.StateAt(199) == 114688 && tokens.StateAt(200) == 229376,
          "LLM partial tokens never yield partial KV state");
    ShadowTaskState state;
    state.costs = CostTier(500000000);
    Near(state.NormalCost(), 0, "OFF no normal cost");
    state.mode = "INITIALIZING";
    state.startNs = 1;
    Near(state.NormalCost(), 0, "incomplete initialization not charged");
    state.mode = "ON";
    state.onNs = 2;
    Near(state.NormalCost(), .0025, "initialization charged once");
    state.localCount = 4;
    state.remoteCount = 2;
    Near(state.NormalCost(), .0085, "completed maintenance counts only");
    ShadowTaskState abort;
    abort.layout = tokens;
    abort.costs = CostTier(tokens.variableBytes);
    abort.BeginInitialization(100, 100, 10, 2);
    abort.StopComputation(200, false, false);
    Check(!abort.CompleteInitialization(300) && abort.mode == "DONE" &&
              abort.summary["initialization_completion_abort"] == true,
          "completion abort cannot resurrect initialization");
    Near(abort.NormalCost(), 0, "completion-aborted initialization incurs no full cost");
    ShadowTaskState miss;
    miss.layout = tokens;
    miss.costs = CostTier(tokens.variableBytes);
    miss.BeginInitialization(100, 100, 10, 2);
    miss.StopComputation(200, true, true);
    Check(!miss.CompleteInitialization(300) && miss.summary["initialization_fault_miss"] == true,
          "fault abort cannot establish protection later");
    Near(miss.NormalCost(), 0, "fault-aborted initialization incurs no full cost");
    ShadowTaskState success;
    success.layout = tokens;
    success.costs = CostTier(tokens.variableBytes);
    success.BeginInitialization(100, 100, 10, 2);
    Check(success.CompleteInitialization(200) && success.localWork == 100 &&
              success.remoteWork == 100,
          "initialization captures START boundary, not current real progress");
    Check(!success.CompleteInitialization(201), "initialization counted once");
    success.pending.emplace_back(200, 114688);
    success.localWork = 200;
    success.delta = 20;
    success.n = 1;
    Check(success.pending.size() == 1 && success.remoteWork == 100,
          "reconfiguration retains pending history");
    success.StopComputation(500, true, true);
    Check(success.mode == "RECOVERING" && success.localWork == 200 && success.remoteWork == 100,
          "fault preserves analytical recovery state");
}
} // namespace

int
main(int argc, char* argv[])
{
    std::string layoutInput, layoutOutput;
    CommandLine command;
    command.AddValue("layoutInput",
                     "Optional frozen task trace for cross-language check",
                     layoutInput);
    command.AddValue("layoutOutput", "Exact G1 budgets and boundaries", layoutOutput);
    command.Parse(argc, argv);
    try
    {
        PureChecks();
        if (!layoutInput.empty())
        {
            std::ifstream stream(layoutInput);
            const auto source = nlohmann::json::parse(stream);
            nlohmann::json result = nlohmann::json::array();
            for (const auto& row : source.at("tasks"))
            {
                TaskDefinition task;
                task.inputBytes = row.at("input_bytes");
                task.computeWorkUnits = row.at("compute_work_units");
                const auto profile = row.at("task_profile").get<std::string>();
                task.taskProfile = profile == "dense-image"        ? TaskProfile::DENSE_IMAGE
                                   : profile == "sparse-inference" ? TaskProfile::SPARSE_INFERENCE
                                   : profile == "compression"      ? TaskProfile::COMPRESSION
                                                                   : TaskProfile::LLM;
                const auto layout = MakeWorkloadLayout(task);
                std::vector<uint64_t> state;
                for (auto work : layout.boundaries)
                    state.push_back(layout.StateAt(work));
                std::vector<uint64_t> targets;
                for (int permille : {50, 100, 200})
                    targets.push_back(*layout.NextProgress(0, permille, 0));
                result.push_back({{"task_id", row.at("task_id")},
                                  {"K", layout.variableBytes},
                                  {"boundaries", layout.boundaries},
                                  {"state_bytes", state},
                                  {"first_targets", targets}});
            }
            std::ofstream out(layoutOutput);
            out << result.dump() << '\n';
            if (!out)
                throw std::runtime_error("cannot write layout test output");
        }
        std::cout << "CompFRR shadow pure-model checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
