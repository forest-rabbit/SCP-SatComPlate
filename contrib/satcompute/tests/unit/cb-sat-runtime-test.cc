/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "ns3/cb-sat-manager.h"
#include "ns3/fa-first-feasible-placement-policy.h"
#include "ns3/fa-least-recovery-load-placement-policy.h"
#include "ns3/first-feasible-placement-policy.h"
#include "ns3/least-recovery-load-placement-policy.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;
using namespace ns3::protection::checkbullet;
namespace
{
uint64_t checks{};
constexpr int64_t END = 3000000000LL;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void Reset()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}
TaskDefinition Definition(TaskProfile profile)
{
    const uint64_t bytes = profile == TaskProfile::LLM ? 400 : 52428800;
    const uint64_t work = profile == TaskProfile::LLM ? 100000 : (bytes * 3 + 1999) / 2000;
    return {1, 0, 3, 0, bytes, 4, work, 1, 1, 2, profile};
}

/** This driver observes actual registration/receipts; it never invents network completion. */
struct Driver
{
    CbSatManager& manager;
    Ptr<TaskCoordinator> tasks;
    std::string mode;
    bool injected{}, froze{};
    bool sharedSeen{};
    std::optional<CbRecoverySnapshot> frozen;
    std::optional<uint64_t> pressure;
    uint32_t pressureNode{};

    void Poll()
    {
        const auto now = Simulator::Now().GetNanoSeconds();
        if (mode == "failed-full" && !injected)
            for (const auto& flow : manager.Flows())
                if (flow.kind == CbFlowKind::INIT_FULL && !tasks->GetTransferEngine()->IsTerminal(flow.transfer))
                {
                    tasks->GetTransferEngine()->FinalizeTransferIfActive(flow.transfer,
                        TransferTerminalState::FAILED, TransferTerminalReason::TASK_FAILED);
                    injected = true;
                    break;
                }
        if (mode == "stop-generation" && !injected && manager.State(1))
        {
            const auto& records = manager.State(1)->Records();
            if (!records.empty() && records.begin()->second.generatedNs > now)
            {
                manager.ReleaseTask(1, "TEST_CLOSE");
                injected = true;
            }
        }
        if (mode == "merge-fault" && !injected)
            for (const auto& event : manager.Events())
                if (event.event == "MERGE_START")
                {
                    const auto cost = manager.Summaries().front().remoteCostNs;
                    const auto at = event.timeNs + cost;
                    if (at < now) continue;
                    Simulator::Schedule(NanoSeconds(at - now), [this, at] {
                        frozen = manager.Freeze(1, at);
                        Check(frozen->state.rootWork < frozen->state.recoverableWork,
                              "same-ns merge fault used the new root");
                        manager.RetainForRecovery(*frozen);
                        const auto& pool = *manager.Pools().at(frozen->state.backupNode);
                        const auto root = pool.Find(frozen->rootObject);
                        Check(root && !root->reserved && root->bytes ==
                            manager.State(1)->FullBytes(frozen->state.rootWork),
                            "same-ns merge deleted physical old root");
                        const auto input = pool.Find(frozen->inputObject);
                        Check(input && input->bytes == manager.State(1)->InputBytes(),
                              "same-ns fault lost complete INPUT");
                        for (const auto& [seq, object] : frozen->logObjects)
                            Check(pool.Find(object) && !pool.Find(object)->reserved,
                                  "same-ns merge fault discarded valid log");
                        froze = true;
                    });
                    injected = true;
                    break;
                }
        const auto snapshot = manager.Snapshot(1);
        if (mode == "two-owners" && tasks->GetTaskRuntimes().at(0).state == TASK_RUNNING &&
            tasks->GetTaskRuntimes().at(1).state == TASK_RUNNING && snapshot && snapshot->Protected())
        {
            const auto second = manager.Snapshot(2);
            if (second && second->Protected())
            {
                const auto node = snapshot->backupNode;
                Check(second->backupNode == node, "controlled shared owner placement not established");
                Check(manager.Quota(node, 1) + manager.Quota(node, 2) == manager.Pools().at(node)->Capacity(),
                      "CB owners independently promised all free space");
                Check(manager.Quota(node, 1) >= manager.Occupied(node, 1) &&
                      manager.Quota(node, 2) >= manager.Occupied(node, 2), "CB quota evicted existing owner");
                sharedSeen = true;
            }
        }
        if (snapshot && snapshot->Protected())
        {
            Check(snapshot->rootWork <= snapshot->recoverableWork, "CB root exceeds q");
            // Before a fault, an independently stored INPUT never shrinks as the root grows.
            if (!froze && tasks->GetTaskRuntimes().front().state == TASK_RUNNING)
            {
                for (const auto& event : manager.Events())
                    if (event.event == "STORAGE_USED" && event.role == "INPUT")
                    {
                        const auto input = manager.Pools().at(event.node)->Find(event.object);
                        Check(input && input->bytes == tasks->GetTaskRuntimes().front().definition.inputBytes,
                              "CB INPUT was trimmed or included in the root");
                    }
            }
        }
        if (now + 50000 < END && !froze)
            Simulator::Schedule(NanoSeconds(50000), &Driver::Poll, this);
    }
};

void Run(TaskProfile profile, const std::string& mode, PlacementPolicy& placement,
         uint64_t capacity = 10000000000ULL)
{
    {
        std::cout << "CB-Sat runtime case: " << TaskProfileToString(profile) << ' '
                  << mode << ' ' << placement.Name() << std::endl;
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        config.parameters.fixedDelaySeconds = 0.001;
        config.parameters.islBandwidthBps = 10000000000ULL;
        config.parameters.routingMode = "global-capacity-aware-hrw";
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = CreateObject<TaskCoordinator>();
        const auto definition = Definition(profile);
        TaskTrace trace{{definition}};
        if (mode == "two-owners")
        {
            auto second = definition;
            second.taskId = 2;
            second.computeNodeId = 4;
            second.inputTransferId = 3;
            second.resultTransferId = 4;
            trace.tasks.push_back(second);
        }
        tasks->Initialize(ComputeProfile{{{0, 100000}, {2, 100000}, {3, 100000}, {4, 100000}}},
            trace, topology, "size-aware", 1024,
            config.parameters.islMtuBytes, config.parameters.receiverRcvBufBytes, false, END);
        PlacementLoadLedger loads;
        CbSatManager manager(tasks, topology, capacity, END,
            mode == "infinite" ? std::numeric_limits<double>::infinity() : 1.0, placement, loads);
        if (mode == "blocked-first")
            Check(manager.Reserve(999, 0, "TEST_OCCUPIED", capacity).has_value(),
                  "controlled first candidate storage not occupied");
        Driver driver{manager, tasks, mode};
        Simulator::Schedule(NanoSeconds(50000), &Driver::Poll, &driver);
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        if (mode == "blocked-first") manager.ReleaseTask(999, "TEST_RELEASE");
        manager.Finalize();
        Check(manager.IsQuiescent() && loads.Empty(), "normal CB resources leaked");
        Check(tasks->GetTaskRuntimes().front().state == TASK_COMPLETED,
              "CB maintenance changed normal computation or RESULT");
        const auto summary = manager.Summaries().front();
        const auto layout = TaskStateAdapter(definition);
        Check(summary.normalCostNs == summary.generated * summary.localCostNs +
              (summary.initialCommits + summary.merges) * summary.remoteCostNs,
              "completed normal operations charged incorrectly");
        const auto& task = tasks->GetTaskRuntimes().front();
        Check(task.computeCompleteTimeNs - task.computeStartTimeNs ==
                  ComputeService::CalculateServiceTimeNs(layout.Work(), 100000),
              "asynchronous cL/cR paused primary compute");
        if (mode == "two-owners")
        {
            Check(driver.sharedSeen, "two-owner dynamic quota path not exercised");
            Check(tasks->GetTaskRuntimes().at(1).state == TASK_COMPLETED, "second ordinary task failed");
        }
        if (mode == "infinite" || capacity == 0 ||
            (mode == "blocked-first" && placement.Eligibility() == PlacementEligibility::MINIMAL))
        {
            Check(manager.Flows().empty() && summary.generated == 0,
                  "infeasible/no-period CB created checkpoint traffic");
            Check(!summary.backup, "no-period CB assigned storage owner");
            if (capacity == 0) Check(!manager.Decisions().empty(), "storage rejection not recorded");
        }
        else if (mode == "stop-generation")
        {
            Check(driver.injected && summary.generated == 0 && summary.normalCostNs == 0,
                  "cancelled cL was counted as completed generation");
        }
        else
        {
            Check(summary.initializedNs >= 0 && summary.initialCommits == 1,
                  "CB failed to initialize full input/root");
            Check(summary.backup && *summary.backup != definition.computeNodeId,
                  "CB backup equals primary");
            Check(manager.State(1)->Records().size() == summary.generated,
                  "generation count differs from materialized records");
            if (mode == "merge-fault") Check(driver.froze, "real merge fault boundary not exercised");
            else Check(summary.merges > 0, "normal X merge did not execute");
            uint64_t inputCount = 0, fullCount = 0;
            for (const auto& flow : manager.Flows())
            {
                if (flow.task != 1) continue;
                Check(flow.source == definition.computeNodeId && flow.destination == *summary.backup,
                      "normal CB used a fake pair or wrong source");
                Check(flow.transfer > definition.resultTransferId, "CB ID overlaps ordinary traffic");
                Check(flow.registeredNs == flow.requestedNs + 1, "noncanonical CB registration phase");
                Check(tasks->GetTransferEngine()->IsRuntimeTransfer(flow.transfer), "CB bypassed real UDP");
                if (flow.kind == CbFlowKind::INIT_INPUT)
                {
                    ++inputCount;
                    Check(flow.bytes == definition.inputBytes, "CB initialization is not full INPUT");
                }
                else
                {
                    const auto& record = manager.State(1)->Records().at(flow.sequence);
                    Check(flow.bytes == record.bytes, "CB network state size differs from record/H");
                    Check(flow.requestedNs >= record.generatedNs, "CB sent before asynchronous cL");
                    if (flow.kind == CbFlowKind::INIT_FULL)
                    {
                        ++fullCount;
                        Check(flow.sequence == 1 && flow.bytes == layout.StateBytes(record.key.toWork) +
                            layout.HeaderBytes(), "CB FULL double-counts INPUT or first delta");
                    }
                    else Check(flow.kind == CbFlowKind::DELTA && flow.sequence > 1,
                               "normal CB invented recovery/tail traffic");
                }
            }
            Check(inputCount == 1, "CB periodically resent INPUT");
            Check(fullCount == (mode == "failed-full" ? 2 : 1), "CB root retransmission identity mismatch");
            if (mode == "failed-full")
            {
                Check(driver.injected, "real failed FULL transfer not exercised");
                std::vector<CbFlow> full;
                for (const auto& f : manager.Flows()) if (f.kind == CbFlowKind::INIT_FULL) full.push_back(f);
                Check(!full.front().completed && full.back().completed &&
                      full.front().sequence == full.back().sequence &&
                      full.front().bytes == full.back().bytes, "retry replaced immutable captured FULL");
                const auto& targets = summary.interval.targets;
                const auto retryAt = summary.startNs +
                    ComputeService::CalculateServiceTimeNs(targets.at(1).work, summary.rate);
                Check(full.back().requestedNs == retryAt, "failed FULL retried outside next H boundary");
            }
        }
        for (const auto& event : manager.Events())
            Check(event.used <= capacity && event.reserved <= capacity - event.used,
                  "physical CB used+reserved exceeds node capacity");
        for (const auto& selection : placement.Selections())
            Check(!selection.pair, "CB single backup reported fabricated pair");
        tasks->Dispose();
    }
    Reset();
}
} // namespace

int main()
{
    try
    {
        for (auto profile : {TaskProfile::DENSE_IMAGE, TaskProfile::SPARSE_INFERENCE,
                             TaskProfile::COMPRESSION, TaskProfile::LLM})
        {
            FaFirstFeasiblePlacementPolicy placement;
            Run(profile, "normal", placement);
        }
        for (const auto& mode : {"failed-full", "stop-generation", "merge-fault", "infinite"})
        {
            FaFirstFeasiblePlacementPolicy placement;
            Run(TaskProfile::LLM, mode, placement);
        }
        { FaFirstFeasiblePlacementPolicy placement; Run(TaskProfile::LLM, "no-storage", placement, 0); }
        { FirstFeasiblePlacementPolicy placement; Run(TaskProfile::LLM, "normal", placement); }
        { FirstFeasiblePlacementPolicy placement; Run(TaskProfile::LLM, "blocked-first", placement); }
        { FaFirstFeasiblePlacementPolicy placement; Run(TaskProfile::LLM, "blocked-first", placement); }
        { FaFirstFeasiblePlacementPolicy placement; Run(TaskProfile::LLM, "two-owners", placement); }
        { LeastRecoveryLoadPlacementPolicy placement(1); Run(TaskProfile::LLM, "normal", placement); }
        { FaLeastRecoveryLoadPlacementPolicy placement(1); Run(TaskProfile::LLM, "normal", placement); }
        std::cout << "CB-Sat runtime: " << checks << " invariant checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "CB-Sat runtime FAILED: " << error.what() << '\n';
        Simulator::Destroy();
        return 1;
    }
}
