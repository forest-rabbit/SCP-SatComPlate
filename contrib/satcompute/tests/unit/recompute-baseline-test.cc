/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "../support/fault-injection.h"
#include "ns3/command-line.h"
#include "ns3/fa-first-feasible-placement-policy.h"
#include "ns3/fa-least-recovery-load-placement-policy.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/protection-metrics.h"
#include "ns3/recompute-controller.h"
#include "ns3/simulator.h"
#include <iostream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;

namespace
{
uint64_t checks{};
void Check(bool value, const std::string& message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}

FaultDefinition Fault(uint64_t id, uint32_t node, int64_t at, bool permanent = false)
{
    FaultDefinition f;
    f.faultId = id;
    f.nodeId = node;
    f.startTimeNs = at;
    f.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
    f.f1Occurred = !permanent;
    if (!permanent)
    {
        f.durationNs = 200000000;
        f.failureProbability = 0.2;
    }
    return f;
}

struct Options
{
    std::string name;
    bool noFault{}, local{}, busyFirst{}, noCapacity{}, recoveryF3{}, recoveryComputeFault{};
    int64_t stopNs{2000000000};
    double deadlineFactor{1.3};
    bool minimal{}, lrl{}, slowFirst{};
};

RecoverySummary Run(const Options& o, const std::string& output)
{
    constexpr int64_t faultNs = 100000000;
    auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", o.stopNs, o.stopNs, 6171353);
    config.parameters.fixedDelaySeconds = .001;
    config.parameters.islBandwidthBps = 10000000000ULL;
    config.parameters.routingMode = "global-capacity-aware-hrw";
    OnlineTopologyController topology(config.parameters, config.constellation);
    topology.Initialize();
    auto tasks = CreateObject<TaskCoordinator>();
    ComputeProfile profile{{{0, o.slowFirst ? 1u : 100000u}, {2, 100000}, {3, 100000}, {4, 100000}}};
    TaskTrace trace{{{1, o.local ? 0u : 1u, 3, o.local ? 0u : 6u,
                     1000000, 4, 100000, 1, 1, 2, TaskProfile::LLM}}};
    tasks->Initialize(profile, trace, topology, "size-aware", 1024,
        config.parameters.islMtuBytes, config.parameters.receiverRcvBufBytes, false,
        o.stopNs, o.deadlineFactor);
    auto fault = CreateObject<FaultController>();
    std::vector<uint32_t> ids;
    for (uint32_t id = 0; id < 16; ++id) ids.push_back(id);
    std::vector<FaultDefinition> faults;
    if (!o.noFault) faults.push_back(Fault(1, 3, faultNs));
    if (o.recoveryF3) faults.push_back(Fault(2, 0, 110000000, true));
    if (o.recoveryComputeFault) faults.push_back(Fault(2, 0, 110000000));
    FaultControllerTestAccess::Schedule(fault, faults, ids, o.stopNs);
    fault->BindTopology(topology);
    fault->BindTaskCoordinator(tasks);
    std::unique_ptr<PlacementPolicy> placement;
    if (o.minimal && o.lrl) placement = std::make_unique<LeastRecoveryLoadPlacementPolicy>(1);
    else if (o.minimal) placement = std::make_unique<FirstFeasiblePlacementPolicy>();
    else if (o.lrl) placement = std::make_unique<FaLeastRecoveryLoadPlacementPolicy>(1);
    else placement = std::make_unique<FaFirstFeasiblePlacementPolicy>();
    RecomputeController controller(tasks, topology, o.stopNs, std::move(placement));
    Simulator::Schedule(NanoSeconds(faultNs - 1), [&] {
        Check(controller.Manager().Events().empty() && controller.Manager().Flows().empty() &&
              controller.Manager().Summaries().empty(), o.name + ": prefault activity");
        Check(controller.Recovery().Summaries().empty(), "recovery before fault");
        if (o.busyFirst)
            Check(tasks->GetComputeServices().front()->ReserveRecovery(99, 1), "cannot reserve first candidate");
        if (o.noCapacity)
        {
            // Physical INPUT path exists, but no idle candidate is available.
            for (auto service : tasks->GetComputeServices())
                if (service->GetNodeId() != 3)
                    Check(service->ReserveRecovery(98, 1), "cannot exhaust idle candidates");
        }
    });
    Simulator::Stop(NanoSeconds(o.stopNs));
    Simulator::Run();
    controller.Finalize();
    Check(controller.PlacementLoads().Empty(), "R0 active placement loads leaked");
    if (o.slowFirst)
    {
        const auto& selected = controller.Placement().Selections();
        Check(selected.size() == 1 && selected.front().node == (o.minimal ? 0u : 2u),
              "R0 minimal/FA admission did not differ or retried another candidate");
        Check((selected.front().admission == "ACCEPTED") == !o.minimal,
              "R0 selected-node true admission missing");
    }
    for (auto service : tasks->GetComputeServices())
    {
        if (o.busyFirst) service->CancelRecovery(99, 1);
        if (o.noCapacity) service->CancelRecovery(98, 1);
        Check(!service->HasRecoveryReservation() && !service->HasRunningTask() &&
              service->GetQueueSize() == 0, "service leaked ownership");
    }
    Check(controller.Manager().Summaries().empty(), "Recompute created checkpoint state");
    for (const auto& [node, pool] : controller.Manager().Pools())
        Check(pool->Capacity() == 0 && pool->PeakTotal() == 0 &&
              pool->AllocationFailures() == 0, "Recompute charged backup storage");
    Check(controller.Manager().IsQuiescent(), "non-quiescent recovery ledger");
    const auto network = tasks->GetTransferEngine();
    const auto capacity = network->CollectCapacityAwareSummary();
    Check(capacity.activePathCountAtEnd == 0 && capacity.totalReservedRateBpsAtEnd == 0 &&
          capacity.pendingTransferCountAtEnd == 0, "network capacity leaked");
    for (const auto& flow : controller.Manager().Flows())
        Check(flow.key.kind == ProtectionTransferKind::RECOVERY_INPUT && flow.storageObject == 0 &&
              flow.bytes == trace.tasks.front().inputBytes, "non-original INPUT or fake checkpoint transfer");
    RecoverySummary r;
    if (o.noFault)
    {
        Check(controller.Recovery().Summaries().empty() && controller.Manager().Flows().empty(),
              "no-fault Recompute did work");
        Check(tasks->IsComplete(), "no-fault primary failed");
    }
    else
    {
        Check(controller.Recovery().Summaries().size() == 1, "not exactly one recovery");
        r = controller.Recovery().Summaries().front();
        Check(r.snapshot.phase == "OFF" && r.normalProtectionCostNs == 0, "prefault protection charged");
        Check(r.plannedCatchupRedoWu == r.snapshot.actualWork && r.plannedTotalRecoveryWu == 100000,
              "planned full catch-up/from-zero work differs");
        Check(r.actualCatchupRedoWu <= r.plannedCatchupRedoWu &&
              r.actualTotalRecoveryWu == r.actualCatchupRedoWu + r.actualPostCatchupWu,
              "actual work conservation");
        if (r.acceptedNs >= 0)
        {
            Check(r.recoveryNode == (o.busyFirst || o.slowFirst ? 2u : 0u), "not first feasible backup node");
            Check(r.plannedInputWaitNs >= 0, "planned INPUT estimate missing");
            Check(r.reservedIdleNs == (r.computeStartedNs < 0 ? r.terminalNs : r.computeStartedNs) -
                  r.acceptedNs, "actual reserved wait differs");
            if (r.computeStartedNs >= 0)
                Check(r.computeStartedNs >= r.inputReceivedNs && r.inputReceivedNs >= r.inputStartedNs,
                      "compute before INPUT receive");
            if (o.local)
                Check(r.inputMode == "LOCAL" && r.resultMode == "LOCAL" && !r.resultTransferId &&
                      controller.Manager().Flows().empty(), "same-node delivery created UDP");
            if (r.terminalState == "COMPLETED")
            {
                Check(r.actualTotalRecoveryWu == 100000 && r.computeCompleteNs <= r.snapshot.deadlineNs,
                      "successful full Recompute budget/deadline");
                tasks->ValidateCompleted();
            }
        }
        else
            Check(controller.Manager().Flows().empty() && r.actualTotalRecoveryWu == 0 &&
                  r.reservedIdleNs == 0, "rejected recovery consumed network or compute");
    }
    const auto directory = std::filesystem::path(output) / o.name;
    controller.Recovery().WriteMetrics(directory);
    WriteProtectionMetrics(controller.Manager(), *network, directory);
    for (const auto& row : network->CollectSummaries())
        Check(network->IsTerminal(row.transferId), "flow left active");
    std::cout << o.name << " " << r.terminalState << " planned=" << r.plannedCatchupRedoWu
              << " actual=" << r.actualCatchupRedoWu << '/' << r.actualTotalRecoveryWu << '\n';
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
    return r;
}
} // namespace

int main(int argc, char** argv)
{
    std::string output = "output/recompute-baseline-unit";
    CommandLine command;
    command.AddValue("outputDir", "Test evidence directory", output);
    command.Parse(argc, argv);
    try
    {
        for (bool minimal : {false, true})
            for (bool lrl : {false, true})
            {
                Options o{std::string("ablation-") + (minimal ? "minimal-" : "fa-") + (lrl ? "lrl" : "ffp")};
                o.minimal = minimal; o.lrl = lrl; o.slowFirst = true;
                Run(o, output);
            }
        RecomputePolicy policy;
        ProtectionContext context;
        Check(policy.OnTaskComputeStart(context).kind == ActionKind::NONE &&
              policy.OnProtectionEpoch(context).kind == ActionKind::NONE &&
              policy.OnComputeFault(context).kind == ActionKind::RECOMPUTE, "pure policy contract");
        Run({"no-fault", true}, output);
        const auto full = Run({"network"}, output);
        Check(full.terminalState == "COMPLETED" && full.inputMode == "NETWORK", "full recompute failed");
        Run({"local", false, true}, output);
        Run({"skip-busy", false, false, true}, output);
        Check(Run({"no-candidate", false, false, false, true}, output).acceptedNs < 0,
              "accepted busy node");
        Options infeasible{"deadline-infeasible"};
        infeasible.deadlineFactor = 1;
        Check(Run(infeasible, output).acceptedNs < 0, "ignored deadline feasibility");
        Options truncated{"simulation-cutoff"};
        truncated.stopNs = 150000000;
        const auto partial = Run(truncated, output);
        Check(partial.actualCatchupRedoWu > 0 && partial.actualCatchupRedoWu < partial.plannedCatchupRedoWu &&
              partial.actualPostCatchupWu == 0, "simulation cutoff charged unexecuted work");
        const auto f3 = Run({"recovery-f3", false, false, false, false, true}, output);
        Check(f3.terminalState == "FAILED" && f3.actualCatchupRedoWu > 0 &&
              f3.actualCatchupRedoWu < f3.plannedCatchupRedoWu && f3.actualPostCatchupWu == 0,
              "F3 cutoff charged unexecuted work");
        Check(Run({"recovery-immune", false, false, false, false, false, true}, output).terminalState == "COMPLETED",
              "accepted recovery lost existing compute-fault immunity");
        Options deadline{"deadline-actual-truncation"};
        const auto primaryStart = full.snapshot.deadlineNs - 1300000000;
        deadline.deadlineFactor = static_cast<double>(full.acceptedNs + full.plannedInputWaitNs +
                                                     1000000000 - primaryStart) / 1e9;
        const auto late = Run(deadline, output);
        Check(late.acceptedNs >= 0 && late.terminalState == "FAILED" &&
              late.actualTotalRecoveryWu < late.plannedTotalRecoveryWu,
              "deadline must truncate actual service after optimistic path estimate");
        const auto again = Run({"network-repeat"}, output);
        Check(again.snapshot.actualWork == full.snapshot.actualWork &&
              again.inputReceivedNs == full.inputReceivedNs && again.resultCompleteNs == full.resultCompleteNs &&
              again.actualTotalRecoveryWu == full.actualTotalRecoveryWu, "non-deterministic Recompute");
        std::cout << "Recompute baseline checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
