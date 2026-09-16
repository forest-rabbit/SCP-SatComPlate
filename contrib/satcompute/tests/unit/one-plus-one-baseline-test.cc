/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "../support/fault-injection.h"
#include "ns3/command-line.h"
#include "ns3/fa-first-feasible-placement-policy.h"
#include "ns3/fa-least-recovery-load-placement-policy.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/one-plus-one-controller.h"
#include "ns3/online-topology-controller.h"
#include "ns3/protection-metrics.h"
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

FaultDefinition Fault(uint64_t id, uint32_t node, int64_t at, bool permanent = false, bool f2 = false)
{
    FaultDefinition f;
    f.faultId = id;
    f.nodeId = node;
    f.startTimeNs = at;
    f.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
    f.f1Occurred = !permanent && !f2;
    f.f2Occurred = !permanent && f2;
    if (!permanent)
    {
        f.durationNs = 200000000;
        f.failureProbability = .2;
    }
    return f;
}

struct Options
{
    std::string name;
    std::vector<FaultDefinition> faults;
    uint32_t source{1}, result{6};
    uint64_t replicaRate{100000}, resultBytes{4};
    bool blockAll{}, blockFirst{}, inspectConcurrent{true};
    double deadlineFactor{1.3};
    int64_t stopNs{2000000000};
    bool minimal{}, lrl{}, slowFirst{};
};

struct Evidence
{
    ReplicaSummary task;
    uint64_t businessResultSent{}, extraResultSent{};
};

Evidence Run(const Options& o, const std::string& output)
{
    auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", o.stopNs, o.stopNs, 6171353);
    config.parameters.fixedDelaySeconds = .001;
    config.parameters.islBandwidthBps = 10000000000ULL;
    config.parameters.routingMode = "global-capacity-aware-hrw";
    OnlineTopologyController topology(config.parameters, config.constellation);
    topology.Initialize();
    auto tasks = CreateObject<TaskCoordinator>();
    ComputeProfile profile{{{0, o.slowFirst ? 1 : o.replicaRate}, {2, 100000}, {3, 100000}, {4, 100000}}};
    TaskTrace trace{{{1, o.source, 3, o.result, 1000000, o.resultBytes, 100000, 1, 1, 2, TaskProfile::LLM}}};
    tasks->Initialize(profile, trace, topology, "size-aware", 1024, config.parameters.islMtuBytes,
        config.parameters.receiverRcvBufBytes, false, o.stopNs, o.deadlineFactor);
    auto fault = CreateObject<FaultController>();
    std::vector<uint32_t> ids;
    for (uint32_t id = 0; id < 16; ++id) ids.push_back(id);
    FaultControllerTestAccess::Schedule(fault, o.faults, ids, o.stopNs);
    fault->BindTopology(topology);
    fault->BindTaskCoordinator(tasks);
    std::unique_ptr<PlacementPolicy> placement;
    if (o.minimal && o.lrl) placement = std::make_unique<LeastRecoveryLoadPlacementPolicy>(1);
    else if (o.minimal) placement = std::make_unique<FirstFeasiblePlacementPolicy>();
    else if (o.lrl) placement = std::make_unique<FaLeastRecoveryLoadPlacementPolicy>(1);
    else placement = std::make_unique<FaFirstFeasiblePlacementPolicy>();
    OnePlusOneController controller(tasks, topology, o.stopNs, std::move(placement));
    if (o.blockAll || o.blockFirst)
    {
        Simulator::Schedule(NanoSeconds(1), [&] {
            for (auto service : tasks->GetComputeServices())
                if (service->GetNodeId() != 3 && (o.blockAll || service->GetNodeId() == 0))
                    Check(service->ReserveRecovery(99, 1), "fixture reservation failed");
        });
        Simulator::Schedule(NanoSeconds(50000000), [&] {
            for (auto service : tasks->GetComputeServices()) service->CancelRecovery(99, 1);
        });
    }
    if (o.inspectConcurrent)
        Simulator::Schedule(NanoSeconds(10000000), [&] {
            uint32_t active = 0;
            for (auto service : tasks->GetComputeServices())
                active += service->HasRunningTask() && service->GetRunningTaskId() == 1;
            Check(active == (o.blockAll || (o.minimal && o.slowFirst) ? 1u : 2u), o.name + ": not real concurrent full execution");
        });
    Simulator::Stop(NanoSeconds(o.stopNs));
    Simulator::Run();
    controller.Finalize();
    const auto rows = controller.Manager().Summaries();
    Check(rows.size() == 1, o.name + ": unexpected logical summary count");
    const auto r = rows.front();
    const auto& p = r.attempts[0];
    const auto& b = r.attempts[1];
    Check(r.requested && r.requestedNs == p.computeStartedNs, "request not at first TASK_RUNNING");
    Check(r.deadlineNs == tasks->GetTaskRuntimes().front().computeDeadlineTimeNs, "deadline reset");
    Check(r.admitted == !(o.blockAll || (o.minimal && o.slowFirst)), o.name + ": unexpected admission");
    Check(!r.admitted || b.node == (o.blockFirst || o.slowFirst ? 2u : 0u), "not expected single-node placement");
    Check(controller.Manager().PlacementLoads().Empty(), "R1 active load leaked");
    Check(controller.Manager().Placement().Selections().size() == 1, "R1 selected more than once");
    if (o.minimal && o.slowFirst)
        Check(controller.Manager().Placement().Selections().front().node == 0 &&
              r.admissionReason == "SELECTED_REPLICA_NODE_INPUT_OR_DEADLINE_INFEASIBLE",
              "minimal R1 retried second node or skipped real admission");
    Check(!r.admitted || b.node != p.node, "replica on primary node");
    uint32_t requests = 0, completions = 0, dispatches = 0;
    for (const auto& event : controller.Manager().Events())
    {
        requests += event.event == "REPLICA_REQUESTED";
        dispatches += event.event == "REPLICA_COMPUTE_STARTED";
        completions += event.event == "LOGICAL_RESULT_WINNER";
    }
    Check(requests == 1 && dispatches <= 1, "request retried or third replica created");
    const auto network = tasks->GetTransferEngine();
    Evidence evidence{r};
    uint64_t allSent = 0, businessSent = 0, extraSent = 0;
    for (const auto& flow : network->CollectSummaries())
    {
        Check(network->IsTerminal(flow.transferId), "network transfer not settled");
        allSent += flow.sentApplicationBytes;
        const bool extra = network->IsProtectionTransfer(flow.transferId);
        (extra ? extraSent : businessSent) += flow.sentApplicationBytes;
        if (flow.transferId == p.resultTransfer || flow.transferId == b.resultTransfer)
            (extra ? evidence.extraResultSent : evidence.businessResultSent) += flow.sentApplicationBytes;
    }
    Check(allSent == businessSent + extraSent, "business/extra bytes do not partition physical bytes");
    const bool success = r.terminalState == "COMPLETED";
    Check(completions == (success ? 1u : 0u), "duplicate logical RESULT");
    uint32_t logicalCompletions = 0;
    for (const auto& event : tasks->GetTaskEvents()) logicalCompletions += event.toState == TASK_COMPLETED;
    Check(logicalCompletions == completions, "task completion count differs from winners");
    if (success)
    {
        Check(p.actualWork + b.actualWork >= r.totalWork, "successful redundant WU negative");
        const auto& winner = r.winner == "primary" ? p : b;
        Check(winner.actualWork == r.totalWork && winner.computeCompleteNs <= r.deadlineNs &&
              winner.resultCompleteNs == r.terminalNs, "winner is not valid full execution/delivery");
        Check(evidence.businessResultSent == (winner.resultMode == "LOCAL" ? 0 : r.resultBytes),
              "two business RESULTs or hidden winning bytes");
        tasks->ValidateCompleted();
    }
    uint64_t serviceNs = 0;
    for (auto service : tasks->GetComputeServices())
    {
        Check(!service->HasRunningTask() && !service->HasRecoveryReservation() && !service->GetQueueSize(),
              "compute ownership leaked");
        serviceNs += service->GetBusyTimeNs();
    }
    Check(serviceNs == p.actualServiceNs + b.actualServiceNs, "real service time not conserved");
    Check(controller.Manager().Ledger().Summaries().empty(), "replica created fake checkpoint state");
    for (const auto& [node, pool] : controller.Manager().Ledger().Pools())
        Check(pool->Capacity() == 0 && pool->PeakTotal() == 0 && !pool->AllocationFailures(),
              "replica used checkpoint pool");
    Check(controller.Manager().Ledger().IsQuiescent(), "replica ledger not quiescent");
    const auto capacity = network->CollectCapacityAwareSummary();
    Check(!capacity.activePathCountAtEnd && !capacity.totalReservedRateBpsAtEnd && !capacity.pendingTransferCountAtEnd,
          "network capacity leaked");
    for (const auto& flow : controller.Manager().Ledger().Flows())
        Check(flow.storageObject == 0 && (flow.key.kind == ProtectionTransferKind::REPLICA_INPUT ||
                                         flow.key.kind == ProtectionTransferKind::REPLICA_RESULT),
              "fake checkpoint transfer for replica");
    const auto directory = std::filesystem::path(output) / o.name;
    controller.Manager().WriteMetrics(directory);
    WriteProtectionMetrics(controller.Manager().Ledger(), *network, directory);
    std::cout << o.name << " " << r.terminalState << " winner=" << r.winner
              << " WU=" << p.actualWork << '+' << b.actualWork << " takeover=" << b.takeoverNs << '\n';
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
    return evidence;
}

void PolicyChecks()
{
    FaFirstFeasiblePlacementPolicy placement;
    OnePlusOnePolicy policy(placement);
    ProtectionContext c;
    c.attempt = {1, 0};
    c.primaryNode = 3;
    c.firstComputeStart = true;
    c.candidates = {{4, true, true, true}, {3, true, true, true}, {0, true, true, true}, {2, true, true, true}};
    c.backupNodeFeasible = [](uint32_t node) { return node != 0; };
    const auto a = policy.OnTaskComputeStart(c);
    Check(a.kind == ActionKind::START_REPLICA && a.replicaNode == 2, "single-role operation feasibility");
    Check(policy.OnTaskComputeStart(c).kind == ActionKind::NONE, "duplicate request");
    c.attempt.taskId = 2;
    c.candidates.clear();
    Check(policy.OnTaskComputeStart(c).kind == ActionKind::NONE, "empty candidates admitted");
    c.candidates = {{0, true, true, true}};
    c.backupNodeFeasible = {};
    Check(policy.OnTaskComputeStart(c).kind == ActionKind::NONE, "failed admission retried");
    Check(policy.OnComputeFault(c).kind == ActionKind::NONE && policy.OnProtectionEpoch(c).kind == ActionKind::NONE,
          "one-plus-one added recompute or periodic maintenance");
}
} // namespace

int main(int argc, char** argv)
{
    std::string output = "output/one-plus-one-baseline-unit";
    CommandLine command;
    command.AddValue("outputDir", "Test evidence directory", output);
    command.Parse(argc, argv);
    try
    {
        PolicyChecks();
        for (bool minimal : {false, true})
            for (bool lrl : {false, true})
            {
                Options o{std::string("ablation-") + (minimal ? "minimal-" : "fa-") + (lrl ? "lrl" : "ffp")};
                o.minimal = minimal; o.lrl = lrl; o.slowFirst = true;
                Run(o, output);
            }
        const auto full = Run({"primary-wins"}, output);
        Check(full.task.winner == "primary", "primary-wins fixture missed winner");
        Options fast{"replica-faster"};
        fast.replicaRate = 200000;
        const auto faster = Run(fast, output);
        Check(faster.task.winner == "replica" && faster.task.attempts[0].actualWork < 100000,
              "replica did not cancel primary compute");
        Options local{"local-input-result"};
        local.source = local.result = 0;
        const auto localResult = Run(local, output);
        Check(localResult.task.winner == "replica" && localResult.task.attempts[1].inputMode == "LOCAL" &&
              localResult.task.attempts[1].resultMode == "LOCAL" && !localResult.task.attempts[1].inputTransfer &&
              !localResult.task.attempts[1].resultTransfer, "same-node delivery created UDP");
        Options firstDelivery{"first-result-not-first-compute"};
        firstDelivery.result = 0;
        const auto delivered = Run(firstDelivery, output);
        Check(delivered.task.winner == "replica" && delivered.task.attempts[0].computeCompleteNs <
              delivered.task.attempts[1].computeCompleteNs && delivered.extraResultSent == 4,
              "winner based on compute completion, or losing physical bytes lost");
        const auto primaryFault = Run({"primary-f1", {Fault(1, 3, 100000000)}}, output);
        Check(primaryFault.task.winner == "replica" && primaryFault.task.attempts[1].takeoverNs == 100000000,
              "surviving replica not promoted after fault batch");
        const auto replicaFault = Run({"replica-f2", {Fault(1, 0, 100000000, false, true)}}, output);
        Check(replicaFault.task.winner == "primary" && replicaFault.task.attempts[1].faulted &&
              !replicaFault.task.attempts[1].immune, "normal replica immune to F2");
        Run({"primary-f3", {Fault(1, 3, 100000000, true)}}, output);
        const auto both = Run({"same-ns-f1-f2", {Fault(1, 3, 100000000), Fault(2, 0, 100000000, false, true)}}, output);
        Check(both.task.terminalState == "FAILED" && both.task.attempts[1].takeoverNs < 0 &&
              !both.task.attempts[1].immune, "same-ns primary callback made replica immune early");
        const auto reversed = Run({"same-ns-reversed-ids", {Fault(2, 3, 100000000), Fault(1, 0, 100000000, false, true)}}, output);
        Check(reversed.task.terminalState == both.task.terminalState && reversed.task.attempts[0].actualWork ==
              both.task.attempts[0].actualWork && reversed.task.attempts[1].actualWork == both.task.attempts[1].actualWork,
              "same-ns result depends on fault ordering");
        Check(Run({"same-ns-f3-f1", {Fault(1, 3, 100000000, true), Fault(2, 0, 100000000)}}, output).task.terminalState == "FAILED",
              "F3 primary transition masked replica compute failure");
        Check(Run({"takeover-immune", {Fault(1, 3, 100000000), Fault(2, 0, 110000000, false, true)}}, output).task.winner == "replica",
              "takeover did not inherit accepted recovery immunity");
        Check(Run({"takeover-f3", {Fault(1, 3, 100000000), Fault(2, 0, 110000000, true)}}, output).task.terminalState == "FAILED",
              "takeover immune to F3");
        Options inputOutage{"outage-during-replica-input"};
        inputOutage.faults = {Fault(1, 3, 3000000), Fault(2, 0, 3000000, false, true)};
        inputOutage.inspectConcurrent = false;
        const auto waiting = Run(inputOutage, output);
        Check(waiting.task.winner == "replica" && !waiting.task.attempts[1].faulted &&
              waiting.task.attempts[1].inputReceivedNs < 203000000 &&
              waiting.task.attempts[1].computeStartedNs == 203000000 &&
              waiting.task.attempts[1].takeoverNs == 203000000,
              "normal INPUT outage gained early immunity or ignored dispatch availability");
        Options sourceLoss{"replica-input-source-f3"};
        sourceLoss.faults = {Fault(1, 1, 3000000, true)};
        sourceLoss.inspectConcurrent = false;
        const auto source = Run(sourceLoss, output);
        Check(source.task.winner == "primary" && source.task.attempts[1].faulted &&
              source.task.attempts[1].actualWork == 0, "source F3 did not stop incomplete replica input");
        const auto resultLoss = Run({"result-endpoint-f3", {Fault(1, 6, full.task.attempts[0].computeCompleteNs + 1000, true)}}, output);
        Check(resultLoss.task.terminalState == "FAILED" && resultLoss.task.attempts[0].faulted &&
              resultLoss.task.attempts[1].faulted, "F3 RESULT endpoint did not invalidate both attempts");
        Options blocked{"one-request-no-retry"};
        blocked.blockAll = true;
        Check(!Run(blocked, output).task.admitted, "retried admission when node became idle");
        Options skip{"skip-busy-first"};
        skip.blockFirst = true;
        Run(skip, output);
        Options deadline{"replica-deadline"};
        deadline.faults = {Fault(1, 3, 100000000)};
        deadline.deadlineFactor = 1 + static_cast<double>(full.task.attempts[1].plannedInputWaitNs) / 1e9;
        const auto late = Run(deadline, output);
        Check(late.task.terminalState == "FAILED" && late.task.attempts[1].actualWork < 100000 &&
              late.task.reason == "COMPUTE_DEADLINE_EXCEEDED", "deadline overcharged work");
        Options resultAfterDeadline{"result-after-compute-deadline"};
        resultAfterDeadline.deadlineFactor = 1;
        resultAfterDeadline.blockAll = true;
        const auto after = Run(resultAfterDeadline, output);
        Check(after.task.terminalState == "COMPLETED" && after.task.terminalNs > after.task.deadlineNs &&
              after.task.attempts[0].computeCompleteNs == after.task.deadlineNs, "RESULT incorrectly has compute deadline");
        const auto equality = Run({"fault-at-compute-end", {Fault(1, 3, full.task.attempts[0].computeCompleteNs)}}, output);
        Check(!equality.task.attempts[0].faulted && equality.task.terminalState == "COMPLETED",
              "inclusive compute completion lost to fault UID");
        Options cutoff{"simulation-cutoff"};
        cutoff.stopNs = 50000000;
        Check(Run(cutoff, output).task.terminalState == "FAILED", "simulation cutoff did not clean attempts");
        const auto repeat = Run({"repeat"}, output);
        Check(repeat.task.terminalNs == full.task.terminalNs && repeat.task.attempts[0].actualWork == full.task.attempts[0].actualWork &&
              repeat.task.attempts[1].actualWork == full.task.attempts[1].actualWork, "replica execution not deterministic");
        std::cout << "1+1 baseline checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
