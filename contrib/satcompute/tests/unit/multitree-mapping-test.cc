/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/multitree-published-rule.h"
#include "ns3/compute-service.h"
#include "ns3/fault-para.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>
using namespace ns3;
using namespace ns3::protection::multitree;
namespace
{
unsigned checks = 0;
void Check(bool pass, const char* message)
{
    ++checks;
    if (!pass) throw std::runtime_error(message);
}
void Near(double a, double b) { Check(std::abs(a - b) < 1e-12, "numeric mismatch"); }
void Tree()
{
    using Case = std::tuple<double, double, double, double, Decision>;
    const auto below = [](double x) { return std::nextafter(x, -INFINITY); };
    const auto above = [](double x) { return std::nextafter(x, INFINITY); };
    const std::vector<Case> cases = {
        {2.2, 5, 0, 0.32, Decision::RP}, // Published case study.
        {2.2, 10, below(4.5), 0.9, Decision::RS},
        {2.2, 10, 4.5, 0.9, Decision::RS},
        {2.2, 10, below(5.0), 0, Decision::RS},
        {2.2, 10, 5.0, 0, Decision::RP},
        {below(2.2), 10, 5.0, 0, Decision::RS},
        {above(2.2), 10, 5.0, 0, Decision::RP},
        {2.0, 10, below(8.2), 0, Decision::RS},
        {2.0, 10, 8.2, 0, Decision::RP},
        {2.0, 10, above(8.2), 0, Decision::RP},
        {2.2, 9, below(3.5), 0.3, Decision::RS},
        {2.2, 9, 3.5, 0.3, Decision::RP},
        {2.2, 9, above(3.5), 0.3, Decision::RP},
        {2.2, below(9), 4.0, 0, Decision::RP},
        {2.2, above(9), 4.0, 0.9, Decision::RS},
        {2.2, 9, 0, below(0.3), Decision::RS},
        {2.2, 9, 0, 0.3, Decision::RS},
        {2.2, 9, 0, above(0.3), Decision::RP},
        {2.2, 9, 0, INFINITY, Decision::RP}};
    for (const auto& [ts, iddl, cl, fr, expected] : cases)
    {
        Features f; f.ts = ts; f.iddl = iddl; f.cl = cl; f.fr = fr;
        Check(Evaluate(f).decision == expected, "published branch/equality mismatch");
    }
    Features boundary; boundary.ts = 2.2; boundary.iddl = 10; boundary.cl = below(4.5);
    Check(std::string(Evaluate(boundary).branch) == "D_HIGH_CL_LOW", "CL<4.5 leaf");
    boundary.cl = 4.5;
    Check(std::string(Evaluate(boundary).branch) == "D_HIGH_CL_MID_TS_HIGH_CL_LOW", "CL=4.5 leaf");
    Features invalid; invalid.ts = 2; invalid.iddl = 9; invalid.fr = NAN;
    bool rejected = false;
    try { Evaluate(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "NaN accepted");
}
void Mapping()
{
    const auto ranks = MakeRanks({30, 10, 20, 20, 50});
    Near(Percentile(ranks, 0), 0); Near(Percentile(ranks, 10), 0);
    Near(Percentile(ranks, 20), 0.375); Near(Percentile(ranks, 25), 0.5625);
    Near(Percentile(ranks, 100), 1); Near(Percentile(MakeRanks({7}), 1), 0.5);
    Near(Percentile(MakeRanks({7, 7}), 7), 0.5);
    const auto same = MakeRanks({20, 50, 20, 10, 30});
    for (uint64_t i = 0; i < 70; ++i)
    {
        Near(Percentile(ranks, i), Percentile(same, i));
        Check(Percentile(ranks, i) <= Percentile(ranks, i + 1), "nonmonotonic rank");
    }
    Calibration scale{ranks, MakeRanks({100, 200})};
    TaskDefinition definition; definition.taskId = 1; definition.inputBytes = 20;
    TaskRuntime task(definition); task.computeDeadlineBudgetNs = 100;
    ComputeFailurePredictionInput prediction; prediction.checkIntervalNs = 1'000'000'000;
    const std::map<uint64_t, uint64_t> inputs{{1, 20}, {2, 30}, {3, 50}};
    auto f = MapFeatures(task, {}, inputs, scale, prediction);
    Near(f.ts, 1.75); Near(f.iddl, 5); Near(f.cl, 0); Near(f.fr, 0);
    Near(MapFeatures(task, {2}, inputs, scale, prediction).cl, 2.5);
    f = MapFeatures(task, {1, 2, 3}, inputs, scale, prediction);
    Near(f.cl, 5.5); Check(f.queuedIds == std::vector<uint64_t>({2, 3}), "current task included");
    Near(EquivalentIntensity(0, 1'000'000'000), 0);
    Near(EquivalentIntensity(0.2, 1'000'000'000), -std::log(0.8));
    Near(EquivalentIntensity(0.2, 2'000'000'000), -std::log(0.8) / 2);
    Check(EquivalentIntensity(0.999999, 1'000'000'000) > 10, "near-one risk lost");
    Check(std::isinf(EquivalentIntensity(1, 1'000'000'000)), "q=1 not infinite");
    // F3 has no input to this adapter: changing external F3 parameters cannot affect FR.
    const auto original = MapFeatures(task, {}, inputs, scale, prediction).fr;
    auto parameters = GetDefaultFaultParameters(); parameters.f3.enabled = !parameters.f3.enabled;
    Near(MapFeatures(task, {}, inputs, scale, prediction).fr, original);
    F1SelfStateFaultModel f1(parameters.f1);
    F2RadiationFaultModel f2(parameters.f2);
    prediction.f1Model = &f1; prediction.f2Model = &f2;
    prediction.f1State.stepFailureProbability = 0.2;
    prediction.f2State.stepFailureProbability = 0.1;
    prediction.remainingComputeTimeNs = 100'000'000'000;
    prediction.firstSampleTimeNs = 123;
    f = MapFeatures(task, {}, inputs, scale, prediction);
    Near(f.qComp, 0.28); Near(f.fr, -std::log(0.72));
    prediction.remainingComputeTimeNs = 1; prediction.finishExclusive = true;
    Near(MapFeatures(task, {}, inputs, scale, prediction).fr, f.fr);
    for (const auto q : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        bool invalid = false;
        try { EquivalentIntensity(q, 1'000'000'000); }
        catch (const std::invalid_argument&) { invalid = true; }
        Check(invalid, "invalid q accepted");
    }
    Check(CalculateComputeDeadlineBudgetNs(ComputeService::CalculateServiceTimeNs(1, 3), 1.3)
          == 433333335, "deadline ns rounding changed");
}
struct QueueProbe
{
    Ptr<ComputeService> service;
    std::vector<uint64_t> starts;
    void Start(uint64_t id, uint32_t, int64_t)
    {
        starts.push_back(id);
        const auto ids = service->GetQueuedTaskIds();
        Check(ids.size() == 3 - starts.size(), "queue snapshot size mismatch");
        Check(std::find(ids.begin(), ids.end(), id) == ids.end(), "running task in queue");
        Check(ids == service->GetQueuedTaskIds(), "snapshot mutated queue");
        if (starts.size() == 1) Check(ids == std::vector<uint64_t>({20, 30}), "FCFS snapshot order");
    }
    void Complete(uint64_t, uint32_t, int64_t) {}
};
void Queue()
{
    QueueProbe probe;
    auto node = CreateObject<Node>(); probe.service = CreateObject<ComputeService>();
    probe.service->Configure(1, 1'000'000'000, MakeCallback(&QueueProbe::Start, &probe),
                             MakeCallback(&QueueProbe::Complete, &probe));
    node->AddApplication(probe.service);
    probe.service->SetStartTime(NanoSeconds(0));
    probe.service->SetStopTime(NanoSeconds(20));
    Simulator::Schedule(NanoSeconds(1), [&]() {
        for (auto id : {30, 10, 20}) probe.service->SubmitTask(id, 2, 1);
    });
    Simulator::Stop(NanoSeconds(20)); Simulator::Run();
    Check(probe.starts == std::vector<uint64_t>({10, 20, 30}), "dispatch changed");
    Simulator::Destroy();
}
}
int main()
{
    try { Tree(); Mapping(); Queue(); std::cout << checks << " Multi-tree mapping checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
