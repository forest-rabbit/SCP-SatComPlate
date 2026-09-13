/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/cb-sat-state.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;
using namespace ns3::protection::checkbullet;

namespace
{
uint64_t checks = 0;
/** Explicit assertions also run with release builds. */
void Check(bool condition, const char* name)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(name);
}
/** Invalid state must never silently become a stronger recovery mechanism. */
template <class F> void Reject(F fn)
{
    try { fn(); }
    catch (const std::invalid_argument&) { ++checks; return; }
    throw std::runtime_error("invalid CB input accepted");
}
/** Pure fixtures only: exact production mapping, no formal MTBF calibration substitute. */
TaskDefinition Task(TaskProfile profile = TaskProfile::LLM, uint64_t input = 52428800)
{
    TaskDefinition task;
    task.taskId = 123;
    task.taskProfile = profile;
    task.computeNodeId = 3;
    task.sourceNodeId = 2;
    task.resultNodeId = 4;
    task.inputBytes = profile == TaskProfile::LLM ? 400 : input;
    task.computeWorkUnits = profile == TaskProfile::LLM ? 1000000 : (input * 3 + 1999) / 2000;
    return task;
}

void Intervals()
{
    const TaskStateAdapter layout(Task());
    const auto reference = SolveInterval(layout, 100000, 200);
    Check(reference.intervalSeconds == 1 && reference.taskSeconds == 10 &&
          reference.deltaPermille == 100 && reference.targets.size() == 9,
          "MTBF=200/cost=2.5ms gives a one-second nominal interval");
    for (const auto& target : reference.targets)
        Check(target.work == target.nominalPermille * 1000, "whole-token nominal targets");
    Check(SolveInterval(layout, 100000, 20000).targets.empty(), "g=T0 skips terminal checkpoint");
    Check(SolveInterval(layout, 100000, 30000).targets.empty(), "g>T0 is not clamped");
    Check(SolveInterval(layout, 100000, std::numeric_limits<double>::infinity()).targets.empty(),
          "zero observed failure intensity creates no invented checkpoint");
    Reject([&] { SolveInterval(layout, 100000, 0); });
    Reject([&] { SolveInterval(layout, 100000, -1); });
    Reject([&] { SolveInterval(layout, 100000, std::numeric_limits<double>::quiet_NaN()); });
    for (const auto profile : {TaskProfile::LLM, TaskProfile::DENSE_IMAGE,
                               TaskProfile::SPARSE_INFERENCE, TaskProfile::COMPRESSION})
        for (const auto bytes : {uint64_t{1}, uint64_t{52428800}, uint64_t{1000000000}})
        {
            const TaskStateAdapter candidate(Task(profile, bytes));
            for (const auto mtbf : {0.000001, 1.0, 200.0, 1000000.0})
            {
                const auto solved = SolveInterval(candidate, 100000, mtbf);
                uint64_t previous = 0;
                for (const auto& t : solved.targets)
                {
                    Check(t.work > previous && t.work < candidate.Work(), "strict nonterminal targets");
                    Check(candidate.Floor(t.work) == t.work, "application-aligned targets");
                    Check(t.nominalPermille % solved.deltaPermille == 0, "no rounding drift");
                    previous = t.work;
                }
            }
        }
}

void Thresholds()
{
    const std::vector<uint64_t> logs{10, 20, 30, 40};
    auto x = SolveThreshold(1000, 100, logs, 50, {0, 0, 50, false});
    Check(x.feasible && !x.recoveryLimit && x.value == 4,
          "constant restore has an explicit unbounded XR");
    x = SolveThreshold(1000, 100, logs, 49, {0, 0, 50, false});
    Check(!x.feasible && x.recoveryLimit == 0 && x.value == 0, "infeasible budget is not X=1");
    x = SolveThreshold(1000, 100, logs, 80, {10, 10, 50, false});
    Check(x.value == 2 && x.recoveryLimit == 2 && x.recoveryBinds, "positive per-log read");
    x = SolveThreshold(1000, 100, logs, 90, {10, 10, 30, true});
    Check(x.value == 2 && x.recoveryLimit == 2, "linear merge adds to read exactly once");
    x = SolveThreshold(160, 100, logs, 100, {0, 0, 50, false});
    Check(x.value == 3 && x.storageBinds, "nonuniform log bytes use inclusive capacity boundary");
    x = SolveThreshold(1000, 100, logs, 100, {0, 0, 50, false}, 2);
    Check(x.value == 2 && x.capBinds, "explicit implementation cap");
    Check(!SolveThreshold(99, 100, logs, 100, {}).feasible, "full INPUT/root must fit quota");
    Check(SolveThreshold(100, 100, {}, 100, {}).reason == "NO_REMAINING_LOGS",
          "no extra terminal log or forced merge");
    Reject([&] { SolveThreshold(1000, 100, logs, -1, {}); });
    Reject([&] { SolveThreshold(1000, 100, logs, 1, {0, -1, 0, false}); });
    Check(CbRestoreCosts{10, 5, 20, false}.Duration(0) == 10 &&
          CbRestoreCosts{10, 5, 20, false}.Duration(3) == 45 &&
          CbRestoreCosts{10, 5, 20, true}.Duration(3) == 85, "restore scope and empty logs");
    // Exhaustive small model against a direct definition over every legal prefix.
    for (int base = 0; base < 3; ++base)
        for (int read = 0; read < 3; ++read)
            for (int merge = 0; merge < 3; ++merge)
                for (const bool linear : {false, true})
                    for (int budget = 0; budget < 12; ++budget)
                        for (uint64_t quota = 100; quota <= 201; ++quota)
                        {
                            const CbRestoreCosts cost{base, read, merge, linear};
                            uint64_t expected = 0, occupied = 100;
                            int64_t worst = cost.Duration(0);
                            for (size_t i = 0; i < logs.size(); ++i)
                            {
                                occupied += logs[i];
                                worst = std::max(worst, cost.Duration(i + 1));
                                if (occupied <= quota && worst <= budget)
                                    expected = i + 1;
                            }
                            Check(SolveThreshold(quota, 100, logs, budget, cost).value == expected,
                                  "XR/XS agree with exhaustive prefix constraints");
                        }
    const auto shares = ShareStorage(101, {{1, 20}, {2, 30}, {3, 0}});
    Check(shares.at(1) == 37 && shares.at(2) == 47 && shares.at(3) == 17,
          "used/reserved floor plus equal free shares");
    const auto odd = ShareStorage(102, {{1, 20}, {2, 30}, {3, 0}});
    Check(odd.at(1) == 38 && odd.at(2) == 47 && odd.at(3) == 17, "stable remainder assignment");
    Reject([] { ShareStorage(5, {{1, 4}, {2, 2}}); });
    Reject([] { ShareStorage(5, {{0, 0}}); });
}

void State()
{
    const auto task = Task();
    const auto costs = GetProtectionCosts(TaskStateAdapter(task).VariableBytes());
    CbState state(task, 0, 1);
    const auto root = state.Capture(400000, 400000, 0);
    Check(root.key.sequence == 1 && root.bytes == state.FullBytes(400000), "first object is FULL");
    Check(state.At(0).recoverableWork == 0, "capture is not protection");
    Check(!state.ReceiveInput(1, task.inputBytes - 1, 0), "partial INPUT not complete");
    Check(!state.Receive(root.key, 2, root.generatedNs), "foreign holder cannot supply state");
    auto stale = root.key;
    ++stale.attemptGeneration;
    Check(!state.Receive(stale, 1, root.generatedNs), "stale attempt cannot supply state");
    Reject([&] { state.Receive(root.key, 1, root.generatedNs - 1); });
    Check(state.Receive(root.key, 1, root.generatedNs), "actual root received");
    Check(!state.Receive(root.key, 1, root.generatedNs), "duplicate receipt is inert");
    const auto initAt = root.generatedNs + costs.remoteNs;
    Check(state.CommitInitial(initAt), "root cR completes independently of missing INPUT");
    Check(!state.BeforeFault(initAt).rootReady && state.At(initAt).rootReady,
          "same-ns root excluded from fault");
    Check(!state.At(initAt).Protected(), "root without INPUT is not fully protected");
    Check(state.ReceiveInput(1, task.inputBytes, initAt + 1), "full INPUT completed");
    const auto a = state.Capture(500000, 500000, initAt + 2);
    const auto b = state.Capture(600000, 600000, initAt + 3);
    Check(state.Receive(b.key, 1, b.generatedNs), "out-of-order receipt retained");
    Check(state.At(b.generatedNs).recoverableWork == 400000, "gap cannot advance q");
    Check(!state.BeginMerge(b.key.sequence, b.generatedNs), "cannot merge across a gap");
    const auto receipt = b.generatedNs + 1;
    Check(state.Receive(a.key, 1, receipt), "predecessor closes gap");
    Check(state.BeforeFault(receipt).recoverableWork == 400000 &&
          state.BeforeFault(receipt + 1).recoverableWork == 600000,
          "strict-before contiguous prefix");
    const auto snap = state.At(receipt);
    Check(snap.Protected() && snap.rootWork == 400000 && snap.recoverableWork == 600000,
          "r=40/q=60 recovery does not roll back to r");
    Check(650000 - snap.recoverableWork == 50000, "65 percent fault loses five percent work");
    auto foreign = b.key;
    foreign.toWork = 660000;
    Check(!state.Receive(foreign, 2, receipt), "nearby 66 percent state is not CB state");
    Check(state.At(receipt).recoverableWork == 600000, "no implicit tail acquisition");
    const auto end = state.BeginMerge(b.key.sequence, receipt);
    Check(end && !state.BeginMerge(b.key.sequence, receipt), "one locked merge");
    const auto c = state.Capture(700000, 700000, receipt + 1);
    Check(state.Receive(c.key, 1, c.generatedNs), "in-merge receipt goes to held queue");
    Check(state.At(c.generatedNs).recoverableWork == 600000, "held queue does not advance q");
    Check(state.CommitMerge(*end), "one actual merge completion");
    Check(state.BeforeFault(*end).rootWork == 400000 &&
          state.BeforeFault(*end).recoverableWork == 600000, "same-ns merge retains previous chain");
    Check(state.At(*end).rootWork == 600000 && state.At(*end).recoverableWork == 700000,
          "merge only moves r, queued contiguous log then moves q");
    Check(state.InputBytes() == 400, "merge never trims immutable INPUT");
    state.Stop();
    Check(!state.CommitMerge(*end + 1) && !state.ReceiveInput(1, 400, *end + 1), "stop is final");
    Reject([&] { state.Capture(800000, 800000, *end + 1); });
    Reject([&] { CbState invalid(task, 0, task.computeNodeId); });
    for (const auto profile : {TaskProfile::DENSE_IMAGE, TaskProfile::SPARSE_INFERENCE,
                               TaskProfile::COMPRESSION, TaskProfile::LLM})
    {
        const auto example = Task(profile);
        CbState s(example, 0, 1);
        const auto& layout = s.Layout();
        for (const auto permille : {100, 600, 900})
        {
            const auto w = layout.Floor(layout.Work() * permille / 1000);
            Check(s.InputBytes() == example.inputBytes &&
                  s.FullBytes(w) == layout.StateBytes(w) + layout.HeaderBytes(),
                  "all profiles retain S and keep F separate at every progress");
        }
    }
}
} // namespace

int main()
{
    try
    {
        Intervals();
        Thresholds();
        State();
        std::cout << "CB-Sat G1: " << checks << " invariant checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "CB-Sat G1: " << error.what() << '\n';
        return 1;
    }
}
