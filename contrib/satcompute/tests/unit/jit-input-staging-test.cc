/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/compfrr-frequency-policy.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/random-variable-stream.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;
namespace
{
unsigned checks{};
void Check(bool pass, const char* reason)
{
    ++checks;
    if (!pass) throw std::runtime_error(reason);
}
void Near(double a, double b, const char* reason) { Check(std::abs(a-b) < 1e-12, reason); }
}
int main()
{
    try
    {
        constexpr int64_t second = 1000000000;
        Check(!EvaluateJitTiming(0, second, 3*second, .1, {}).shouldPrefetch, "zero mass");
        Check(EvaluateJitTiming(0, second, 3*second, .1, {{second, .2}}).shouldPrefetch,
              "short INPUT must not miss the next discrete event");
        Check(!EvaluateJitTiming(0, second, 3*second, .1, {{2*second, .2}}).shouldPrefetch,
              "early prefetch");
        Check(EvaluateJitTiming(0, second, 3*second, 1, {{2*second, 1}}).shouldPrefetch,
              "inclusive JIT boundary");
        const auto survived = EvaluateJitTiming(second, 2*second, 3*second, .1,
                                                 {{second, 1}, {2*second, .25}, {3*second, 1}});
        Near(survived.probabilityOn, .25, "sample counted twice or finish included");
        const std::vector<FrequencyRiskStep> example{{second, .2}, {2*second, .125}};
        const auto plan = PlanJitInput(0, 600000000, 3*second, 1, example, true);
        Check(plan.startNs == 600000000, "first legal initialized event");
        Near(plan.deferredLossSeconds, .3, "weighted full INPUT");
        Near(plan.prefetchLossSeconds, .12, "weighted remaining INPUT");
        Near(plan.gainSeconds, .18, "model section 19");
        const auto absent = PlanJitInput(0, 600000000, 3*second, 1, example, false);
        Check(!absent.startNs && absent.gainSeconds == 0, "unadmitted future plan");
        Near(absent.prefetchLossSeconds, absent.deferredLossSeconds, "no-plan degeneration");
        const auto local = PlanJitInput(0, 600000000, 3*second, 0, example, true);
        Near(local.gainSeconds, 0, "local INPUT must not invent network benefit");
        auto rng = CreateObject<UniformRandomVariable>();
        auto control = CreateObject<UniformRandomVariable>();
        rng->SetStream(79); control->SetStream(79);
        for (int i = 0; i < 100; ++i)
        {
            PlanJitInput(0, 600000000, 3*second, 1, example, true);
            Check(rng->GetValue() == control->GetValue(), "planner consumed RNG");
        }
        FrequencyInput in;
        in.inputPolicy = InputStagingPolicy::DEFERRED;
        in.risk = {0, second, .2, .3, example};
        in.inputBytes = 100; in.work = 1000; in.variableBytes = 100;
        in.primaryRate = in.recoveryRate = 100;
        in.inputBandwidth = in.backupBandwidth = 1000;
        in.remainingSeconds = 10; in.deadlineNs = 20*second;
        in.costs = {100000, 500000};
        in.stateTransferSeconds = .1;
        in.nodeAvailable = in.pathAvailable = true;
        in.localFreeBytes = in.remoteFreeBytes = 1000000;
        in.storageDemand = [](auto) { return std::optional(FrequencyStorageDemand{100,100}); };
        CompFrrFrequencyPolicy solver;
        const auto v6 = solver.Evaluate(in);
        in.inputPolicy = InputStagingPolicy::JIT;
        const auto degenerate = solver.Evaluate(in);
        Check(v6.action == degenerate.action && v6.jStart == degenerate.jStart &&
              v6.jOff == degenerate.jOff && v6.selected->config == degenerate.selected->config,
              "V7 no-plan must exactly degenerate to V6");
        in.jitPrefetchAdmissible = true;
        const auto v7 = solver.Evaluate(in);
        Check(v7.selected->config == v6.selected->config, "JIT changed frequency ranking");
        Check(v7.inputPlan && v7.inputPlan->gainSeconds > 0, "START missing INPUT gain");
        Near(*v7.jOff - *v6.jOff, v7.inputPlan->gainSeconds, "START relative gain");
        in.phase = ProtectionPhase::ON;
        in.actualInputWaitSeconds = 0;
        const auto onReady = solver.Evaluate(in);
        in.actualInputWaitSeconds = .1;
        const auto onAbsent = solver.Evaluate(in);
        Check(onReady.selected->config == onAbsent.selected->config &&
              onReady.selected->objective == onAbsent.selected->objective,
              "common INPUT wait must not change ON score");
        Check(StateOnlyInitialization(InputStagingPolicy::JIT) &&
              !StateOnlyInitialization(InputStagingPolicy::EAGER), "layout enum contract");
        in.phase = ProtectionPhase::OFF; in.jitPrefetchAdmissible = false;
        in.risk.futureSteps.push_back({10*second, .4}); in.risk.pFailBeforeFinish = .58;
        in.inputPolicy = InputStagingPolicy::DEFERRED;
        const auto endpointV6 = solver.Evaluate(in);
        in.inputPolicy = InputStagingPolicy::JIT;
        const auto endpointV7 = solver.Evaluate(in);
        Check(endpointV6.jOff == endpointV7.jOff && endpointV6.jStart == endpointV7.jStart &&
              endpointV6.selected->config == endpointV7.selected->config, "zero gain changed canonical START endpoint");
        Near(endpointV7.inputPlan->readyProbability, *endpointV7.pFailAfterInitReady,
             "planner changed original START window probability");
        std::cout << "JIT pure policy: " << checks << " checks passed.\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
