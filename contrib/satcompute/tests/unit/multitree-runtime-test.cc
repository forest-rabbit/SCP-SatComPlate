/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "../support/fault-injection.h"
#include "ns3/multitree-controller.h"
#include "ns3/fa-first-feasible-placement-policy.h"
#include "ns3/online-topology-controller.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/command-line.h"
#include "ns3/simulator.h"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace ns3;
using namespace ns3::protection;
using namespace ns3::protection::multitree;
namespace
{
unsigned checks{};
void Check(bool value, const std::string& message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
FaultDefinition Fault(uint64_t id, uint32_t node, int64_t at, bool permanent = false)
{
    FaultDefinition fault;
    fault.faultId = id; fault.nodeId = node; fault.startTimeNs = at;
    fault.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
    fault.f1Occurred = !permanent;
    if (!permanent) { fault.durationNs = 200'000'000; fault.failureProbability = .2; }
    return fault;
}
struct Options
{
    std::string name;
    bool primaryFault{}, rsFault{}, backupFault{}, permanent{}, blocked{}, local{};
    int64_t faultAtNs{100'000'000};
};
void Run(const Options& o, const std::filesystem::path& root)
{
    RngSeedManager::SetSeed(1); RngSeedManager::SetRun(11); RngSeedManager::ResetNextStreamIndex();
    constexpr int64_t stopNs = 8'000'000'000;
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", stopNs, stopNs, 6171353);
        config.parameters.fixedDelaySeconds = .001;
        config.parameters.islBandwidthBps = 10'000'000'000;
        config.parameters.routingMode = "global-capacity-aware-hrw";
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = CreateObject<TaskCoordinator>();
        ComputeProfile profile{{{0, 100000}, {2, 100000}, {3, 100000}, {4, 100000}}};
        TaskTrace trace;
        for (uint64_t id = 10; id < 16; ++id)
            trace.tasks.push_back({id, o.local ? 0u : 1u, 3, o.local ? 0u : 6u,
                1000000, 4, 100000, 1, 2 * id, 2 * id + 1, TaskProfile::LLM});
        trace.tasks.push_back({80, 1, 4, 6, 1000000, 4, 100000, 30'000'000, 160, 161, TaskProfile::LLM});
        tasks->Initialize(profile, trace, topology, "size-aware", 1024,
            config.parameters.islMtuBytes, config.parameters.receiverRcvBufBytes, false, stopNs, 1.3);
        auto fault = CreateObject<FaultController>();
        std::vector<FaultDefinition> faults;
        if (o.primaryFault) faults.push_back(Fault(100, 3, o.faultAtNs, o.permanent));
        if (o.rsFault) faults.push_back(Fault(101, 4, o.faultAtNs));
        if (o.backupFault) faults.push_back(Fault(102, 0, o.faultAtNs, o.permanent));
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        FaultControllerTestAccess::Schedule(fault, faults, ids, stopNs);
        fault->BindTopology(topology); fault->BindTaskCoordinator(tasks);
        auto model = CreateObject<FaultModelEngine>();
        auto parameters = GetDefaultFaultParameters();
        parameters.f1.temperature.heatingToCriticalSeconds = 100000;
        parameters.f2.enabled = false; parameters.f3.enabled = false;
        model->Configure(parameters, ids, {0, 2, 3, 4}, stopNs, fault, false);
        model->BindTaskCoordinator(tasks);
        MultiTreeController controller(tasks, topology, model, stopNs,
                                        std::make_unique<FaFirstFeasiblePlacementPolicy>());
        Simulator::Schedule(NanoSeconds(1), [&] {
            for (auto service : tasks->GetComputeServices())
                if (service->GetNodeId() == 3 || o.blocked)
                    Check(service->ReserveRecovery(999, 1), "fixture hold failed");
        });
        Simulator::Schedule(NanoSeconds(20'000'000), [&] {
            for (auto service : tasks->GetComputeServices())
                if (service->GetNodeId() == 3) service->ReleaseRecovery(999, 1);
        });
        if (o.blocked)
            Simulator::Schedule(NanoSeconds(400'000'000), [&] {
                for (auto service : tasks->GetComputeServices()) service->ReleaseRecovery(999, 1);
            });
        Simulator::Stop(NanoSeconds(stopNs)); Simulator::Run();
        controller.Finalize(); controller.Finalize();
        controller.WriteMetrics(root / o.name);
        Check(controller.Decisions().Get(10) == Decision::RP, "causal queued primary did not select RP");
        Check(controller.Decisions().Get(80) == Decision::RS, "empty-queue primary did not select RS");
        const auto recovery = controller.Resubmission().Recovery().Summaries();
        const auto replica = controller.Replication().Summaries();
        Check(!replica.empty(), "no RP requests");
        const auto first = std::find_if(replica.begin(), replica.end(), [](const auto& r) { return r.taskId == 10; });
        Check(first != replica.end(), "missing first RP");
        Check(first->admitted == !o.blocked, "RP actual admission differs");
        for (const auto& row : recovery)
        {
            Check(controller.Decisions().Get(row.snapshot.taskId) == Decision::RS, "RP acquired hidden RS fallback");
            Check(row.snapshot.phase == "OFF" && row.plannedTotalRecoveryWu == 100000,
                  "RS acquired checkpoint state/partial workload");
        }
        for (const auto& row : replica)
        {
            Check(controller.Decisions().Get(row.taskId) == Decision::RP, "RS requested a replica");
            if (row.attempts[1].computeStartedNs >= 0)
                Check(row.attempts[1].inputReceivedNs >= 0 &&
                      row.attempts[1].computeStartedNs >= row.attempts[1].inputReceivedNs,
                      "replica computed before real INPUT dependency");
            if (row.attempts[1].takeoverNs >= 0)
                Check(row.attempts[1].takeoverNs >= o.faultAtNs, "early takeover/immunity");
        }
        Check(recovery.size() == static_cast<size_t>(o.rsFault), "duplicate/missing RS recovery");
        if (o.primaryFault && o.faultAtNs < 1'020'000'000)
        {
            if (o.blocked || o.backupFault)
                Check(first->terminalState == "FAILED" && first->attempts[1].takeoverNs < 0,
                      "rejected/failed RP survived or promoted before batch");
            else
                Check(first->winner == "replica" && first->terminalState == "COMPLETED" &&
                      first->attempts[1].takeoverNs == o.faultAtNs, "valid RP did not take over");
        }
        if (o.local)
            Check(first->attempts[1].inputMode == "LOCAL" && !first->attempts[1].inputTransfer &&
                  first->attempts[1].resultMode == "LOCAL" && !first->attempts[1].resultTransfer,
                  "same-star delivery created UDP");
        if (o.faultAtNs == 1'020'000'000)
        {
            Check(!first->attempts[0].faulted && first->attempts[0].computeCompleteNs == o.faultAtNs,
                  "inclusive primary completion lost to same-ns fault");
            const auto& allTasks = tasks->GetTaskRuntimes();
            const auto next = std::find_if(allTasks.begin(), allTasks.end(),
                [](const auto& task) { return task.definition.taskId == 11; });
            Check(next != allTasks.end() && next->computeStartTimeNs >= o.faultAtNs + 200'000'000,
                  "next queued task started inside the fault batch/outage");
        }
        std::set<uint64_t> transferIds;
        size_t rpFlows = 0, rsFlows = 0;
        for (const auto& flow : tasks->GetTransferEngine()->CollectSummaries())
        {
            Check(transferIds.insert(flow.transferId).second, "duplicate physical transfer ID");
            Check(tasks->GetTransferEngine()->IsTerminal(flow.transferId), "unsettled flow");
        }
        for (const auto& flow : controller.Resubmission().Manager().Flows())
        {
            Check(flow.storageObject == 0, "Multi-tree allocated checkpoint object");
            rpFlows += flow.key.kind == ProtectionTransferKind::REPLICA_INPUT;
            rsFlows += flow.key.kind == ProtectionTransferKind::RECOVERY_INPUT;
        }
        if (o.rsFault && !o.blocked && !o.local)
            Check(rpFlows && rsFlows, "mixed transport did not share unique live IDs");
        Check(controller.Resubmission().Manager().IsQuiescent(), "final ownership leak");
        Check(controller.Resubmission().PlacementLoads().Empty(), "final load leak");
        for (const auto& [node, pool] : controller.Resubmission().Manager().Pools())
            Check(pool->PeakTotal() == 0, "hidden checkpoint storage");
        std::map<uint64_t, unsigned> terminals, requests;
        for (const auto& event : tasks->GetTaskEvents())
            if (IsTerminalTaskState(event.toState)) ++terminals[event.taskId];
        for (const auto& event : controller.Replication().Events())
            if (event.event == "REPLICA_REQUESTED") ++requests[event.taskId];
        for (const auto& task : tasks->GetTaskRuntimes()) Check(terminals[task.definition.taskId] == 1, "logical terminal duplication");
        for (const auto& [id, count] : requests) Check(count == 1, "RP admission retried");
    }
    Simulator::Destroy(); Ipv4AddressGenerator::Reset(); Mac48Address::ResetAllocationIndex();
}
}
int main(int argc, char** argv)
{
    std::string output;
    CommandLine command; command.AddValue("outputDir", "Fixture evidence directory", output); command.Parse(argc, argv);
    try
    {
        if (output.empty()) throw std::invalid_argument("outputDir required");
        Run({"normal"}, output);
        Run({"mixed", true, true}, output);
        Run({"rejected", true, false, false, false, true}, output);
        Run({"same-batch", true, true, true}, output);
        Run({"permanent", true, true, false, true}, output);
        Run({"local", true, true, false, false, false, true}, output);
        Run({"completion-tie", true, false, false, false, false, false, 1'020'000'000}, output);
        std::cout << checks << " Multi-tree runtime checks passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
