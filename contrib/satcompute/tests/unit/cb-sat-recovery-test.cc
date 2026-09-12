/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "ns3/cb-sat-recovery.h"
#include "ns3/fa-first-feasible-placement-policy.h"
#include "ns3/fault-controller.h"
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
    Simulator::Destroy(); Ipv4AddressGenerator::Reset(); Mac48Address::ResetAllocationIndex();
}
struct Options
{
    std::string name{"direct"}, expected{"DIRECT"};
    bool busy{}, success{true}, merge40{true}, primaryF3{}, noCheckpoint{};
    std::string damage;
    RemoteBusyRecoveryPolicy busyPolicy{RemoteBusyRecoveryPolicy::RELOCATE};
    double deadline{1.3};
    uint32_t source{}, result{4};
    int64_t faultDelay{650000000}, stop{END};
    TaskProfile profile{TaskProfile::LLM};
};
struct Driver
{
    CbSatManager& manager;
    CbSatRecovery& recovery;
    Ptr<TaskCoordinator> tasks;
    Ptr<FaultController> fault;
    const Options& options;
    uint64_t nextFault{1};
    int64_t start{-1};
    bool failedTransfer{}, pressureApplied{};

    void Emit(uint32_t node, bool permanent = false, bool f2 = false)
    {
        FaultDefinition f;
        f.faultId = nextFault++;
        f.nodeId = node;
        f.startTimeNs = Simulator::Now().GetNanoSeconds();
        f.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
        f.f1Occurred = !permanent && !f2;
        f.f2Occurred = !permanent && f2;
        if (!permanent) { f.durationNs = 200000000; f.failureProbability = 0.2; }
        fault->SubmitGeneratedBatch({{FaultEventType::START, f}});
    }
    Ptr<ComputeService> Service(uint32_t node)
    {
        for (auto service : tasks->GetComputeServices()) if (service->GetNodeId() == node) return service;
        throw std::runtime_error("test service missing");
    }
    void OnTask(const TaskEventRecord& event)
    {
        if (event.taskId != 1 || event.toState != TASK_RUNNING) return;
        start = event.simulationTimeNs;
        if (options.merge40)
        {
            Simulator::Schedule(NanoSeconds(430000000), [this] {
                const auto snapshot = manager.Snapshot(1);
                Check(snapshot && snapshot->rootWork == 10000 && snapshot->recoverableWork == 40000,
                      "real 10/40 checkpoint pressure fixture missing");
                // Actual temporary competing ownership lowers X; no private policy override.
                auto free = manager.Pools().at(0)->Free();
                while (free > 4000000)
                {
                    Check(manager.Reserve(999, 0, "TEST_PRESSURE", free / 2).has_value(),
                          "shared storage pressure reservation failed");
                    free = manager.Pools().at(0)->Free();
                }
                pressureApplied = true;
            });
            Simulator::Schedule(NanoSeconds(450000000), [this] {
                Check(manager.Snapshot(1)->rootWork == 40000, "X pressure did not materialize root 40");
                manager.ReleaseTask(999, "TEST_PRESSURE_RELEASE");
            });
        }
        if (options.busy)
            Simulator::Schedule(NanoSeconds(options.faultDelay - 50000000), [this] {
                Check(Service(0)->ReserveRecovery(998, 1), "test busy slot not actually reserved");
            });
        if (options.damage == "backup-unavailable")
            Simulator::Schedule(NanoSeconds(options.faultDelay - 10000000), [this] { Emit(0); });
        if (options.damage == "backup-f3-before")
            Simulator::Schedule(NanoSeconds(options.faultDelay - 10000000), [this] { Emit(0, true); });
        Simulator::Schedule(NanoSeconds(options.faultDelay), [this] { Emit(3, options.primaryF3); });
        // Scheduled before the fault creates its +1ns decision: audit only the frozen plan,
        // before any restore can consume its objects. Rejected plans must have no side effects.
        Simulator::Schedule(NanoSeconds(options.faultDelay + 1), [this] {
            const auto snapshot = recovery.Summaries().front().snapshot;
            if (!snapshot.state.rootReady || !snapshot.rootObject ||
                !tasks->IsSatelliteAvailable(snapshot.state.backupNode)) return;
            const auto node = snapshot.state.backupNode;
            const auto& pool = *manager.Pools().at(node);
            const auto used = pool.Used(), reserved = pool.Reserved();
            auto bad = snapshot;
            bad.state.recoverableWork += 400;
            Check(!manager.ApplyStoredLogs(bad, node, snapshot.rootObject, snapshot.logObjects),
                  "executor promoted unapproved q");
            bad = snapshot; ++bad.state.attemptGeneration;
            Check(!manager.ApplyStoredLogs(bad, node, snapshot.rootObject, snapshot.logObjects),
                  "executor accepted wrong generation");
            bad = snapshot; bad.cutoffNs += 1000000000;
            Check(!manager.ApplyStoredLogs(bad, node, snapshot.rootObject, snapshot.logObjects),
                  "executor accepted future observation cutoff");
            Check(!manager.ApplyStoredLogs(snapshot, node, snapshot.inputObject, snapshot.logObjects),
                  "executor treated INPUT as root");
            if (!snapshot.logObjects.empty())
            {
                auto logs = snapshot.logObjects; logs.erase(logs.begin());
                Check(!manager.ApplyStoredLogs(snapshot, node, snapshot.rootObject, logs),
                      "executor filled missing log from another location");
                logs = snapshot.logObjects; logs.begin()->second = snapshot.rootObject;
                Check(!manager.ApplyStoredLogs(snapshot, node, snapshot.rootObject, logs),
                      "executor accepted duplicate backing object");
            }
            Check(pool.Used() == used && pool.Reserved() == reserved,
                  "rejected restore plan partially mutated storage");
        });
        if (options.damage == "recovery-f1" || options.damage == "recovery-f2")
            Simulator::Schedule(NanoSeconds(options.faultDelay + 10000000), [this] {
                const auto row = recovery.Summaries().front();
                Check(row.node.has_value(), "recovery not accepted before immunity test");
                Emit(*row.node, false, options.damage == "recovery-f2");
            });
        if (options.damage == "recovery-f3" || options.damage == "source-f3" || options.damage == "result-f3")
            Simulator::Schedule(NanoSeconds(options.faultDelay + 5000000), [this] {
                const auto row = recovery.Summaries().front();
                Check(row.node.has_value(), "recovery not accepted before F3 test");
                Emit(options.damage == "source-f3" ? 0 : options.damage == "result-f3" ?
                     options.result : *row.node, true);
            });
        if (options.damage == "input-missing" || options.damage == "root-missing")
            Simulator::Schedule(NanoSeconds(100000010), &Driver::Poll, this);
    }
    void Poll()
    {
        const auto wanted = options.damage == "input-missing" ? CbFlowKind::INIT_INPUT : CbFlowKind::INIT_FULL;
        for (const auto& flow : manager.Flows())
            if (flow.kind == wanted && !tasks->GetTransferEngine()->IsTerminal(flow.transfer))
            {
                tasks->GetTransferEngine()->FinalizeTransferIfActive(flow.transfer,
                    TransferTerminalState::FAILED, TransferTerminalReason::TASK_FAILED);
                failedTransfer = true;
                return;
            }
        if (Simulator::Now().GetNanoSeconds() < start + options.faultDelay)
            Simulator::Schedule(NanoSeconds(10000), &Driver::Poll, this);
    }
};

CbRecoverySummary Run(const Options& options)
{
    CbRecoverySummary result;
    {
        std::cout << "CB-Sat recovery case: " << options.name << std::endl;
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", options.stop, options.stop, 6171353);
        config.parameters.fixedDelaySeconds = 0.001;
        config.parameters.islBandwidthBps = 10000000000ULL;
        config.parameters.routingMode = "global-capacity-aware-hrw";
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = CreateObject<TaskCoordinator>();
        const auto bytes = options.profile == TaskProfile::LLM ? 400ULL : 52428800ULL;
        const auto work = options.profile == TaskProfile::LLM ? 100000ULL : (bytes * 3 + 1999) / 2000;
        TaskTrace trace{{{1, options.source, 3, options.result, bytes, 4, work, 1, 1, 2, options.profile}}};
        tasks->Initialize(ComputeProfile{{{0, 100000}, {2, 100000}, {3, 100000}, {4, 100000}}}, trace,
            topology, "size-aware", 1024, config.parameters.islMtuBytes,
            config.parameters.receiverRcvBufBytes, false, options.stop, options.deadline);
        auto fault = CreateObject<FaultController>();
        fault->ConfigureGeneration(topology.GetIdMap().GetCanonicalSatelliteIds(), options.stop);
        fault->BindTopology(topology); fault->BindTaskCoordinator(tasks);
        FaFirstFeasiblePlacementPolicy placement;
        PlacementLoadLedger loads;
        CbSatManager manager(tasks, topology, options.profile == TaskProfile::LLM ? 100000000 : 200000000,
            options.stop, options.noCheckpoint ? std::numeric_limits<double>::infinity() :
                                                8.33333333333334, placement, loads);
        CbSatRecovery recovery(tasks, topology, manager, loads, options.stop, options.busyPolicy);
        Driver driver{manager, recovery, tasks, fault, options};
        tasks->ConnectTaskObserver(MakeCallback(&Driver::OnTask, &driver));
        Simulator::Stop(NanoSeconds(options.stop));
        Simulator::Run();
        recovery.Finalize(); tasks->FinalizeSimulation();
        if (options.busy) driver.Service(0)->CancelRecovery(998, 1);
        manager.ReleaseTask(999, "TEST_END");
        manager.Finalize();
        const auto rows = recovery.Summaries();
        Check(rows.size() == 1, "CB created zero or multiple recovery attempts");
        result = rows.front();
        const auto& task = tasks->GetTaskRuntimes().front();
        Check((task.state == TASK_COMPLETED) == options.success, "CB logical recovery outcome wrong");
        Check(result.path == options.expected, "CB recovery selected wrong path");
        Check(manager.IsQuiescent() && loads.Empty(), "CB recovery storage/flow/load leaked");
        for (auto service : tasks->GetComputeServices())
            Check(!service->HasRecoveryReservation(), "CB recovery compute reservation leaked");
        Check(result.acceptedNs == result.snapshot.cutoffNs + 1, "CB skipped same-ns fault batch boundary");
        Check(result.snapshot.actualWork >= result.resumeWork &&
              result.plannedCatchupWu == result.snapshot.actualWork - result.resumeWork,
              "CB planned catchup is not Wf-q");
        Check(result.actualCatchupWu <= result.plannedCatchupWu &&
              result.actualRecoveryWu == result.actualCatchupWu + result.actualRemainingWu,
              "CB counted planned rather than actual WU");
        if (options.merge40 && options.damage != "backup-f3-before")
        {
            Check(driver.pressureApplied, "real X pressure fixture not applied");
            Check(result.snapshot.state.rootWork == 40000 && result.snapshot.state.recoverableWork == 60000 &&
                  result.snapshot.actualWork == 65000, "fault did not freeze exact 65/40/60 fixture");
        }
        if (result.computeStartedNs >= 0)
        {
            Check(result.computeStartedNs >= std::max(result.inputReadyNs, result.stateReadyNs),
                  "CB resumed before INPUT/state readiness");
            Check(result.reservedIdleNs == result.computeStartedNs - result.acceptedNs,
                  "CB reserved-idle not actual accepted-to-start wait");
        }
        if (options.success)
        {
            Check(result.actualRecoveryWu == work - result.resumeWork, "CB successful remaining WU mismatch");
            Check(result.catchupNs >= result.computeStartedNs && result.computedNs <= task.computeDeadlineTimeNs,
                  "CB did not execute catchup or original deadline");
            Check(task.attemptGeneration == 1 && task.TaskSucceeded(), "CB logical winner is not recovery");
        }
        uint64_t relocation = 0, recoveryInput = 0, resultFlows = 0;
        for (const auto& flow : manager.Flows())
        {
            const std::string name = CbFlowName(flow.kind);
            Check(name.find("TAIL") == std::string::npos, "CB requested forbidden tail");
            Check(flow.source != flow.destination, "CB same-node logical delivery created UDP");
            if (flow.generation != 1) continue;
            if (flow.kind == CbFlowKind::RELOCATE_INPUT || flow.kind == CbFlowKind::RELOCATE_FULL ||
                flow.kind == CbFlowKind::RELOCATE_LOG)
            {
                relocation += flow.bytes;
                Check(flow.source == result.snapshot.state.backupNode && flow.destination == *result.node,
                      "CB migration fetched state from an unapproved source");
            }
            if (flow.kind == CbFlowKind::FALLBACK_INPUT) ++recoveryInput;
            if (flow.kind == CbFlowKind::RESULT)
            {
                ++resultFlows;
                Check(!tasks->GetTransferEngine()->IsProtectionTransfer(flow.transfer),
                      "CB RESULT incorrectly counted as protection traffic");
            }
        }
        if (result.path == "DIRECT" && result.snapshot.state.inputReady)
            Check(recoveryInput == 0 && result.inputMode == "STORED_LOCAL", "CB direct resent complete INPUT");
        if (result.path == "RELOCATE")
        {
            Check(relocation == result.relocationBytes && result.resumeWork == result.snapshot.state.recoverableWork,
                  "CB relocation changed q or declared bytes");
            if (options.success) Check(result.stateReadyNs >= result.stateReceivedNs,
                                       "CB migration materialized before last state receipt");
        }
        else Check(relocation == 0, "non-relocation branch moved state");
        if (options.result == result.node && options.success)
            Check(result.resultMode == "LOCAL" && resultFlows == 0 && result.resultBytes == 4,
                  "local RESULT lost logical bytes or created UDP");
        if (options.damage == "input-missing" || options.damage == "root-missing")
            Check(driver.failedTransfer, "partial initialization failure not physically injected");
        if (result.path == "RECOMPUTE") Check(result.resumeWork == 0, "recompute secretly used checkpoint q");
        if (!options.success && options.damage == "recovery-f3")
            Check(result.actualRecoveryWu < work - result.resumeWork,
                  "F3 cancellation charged all planned WU");
        tasks->DisconnectTaskObserver(MakeCallback(&Driver::OnTask, &driver));
    }
    Reset();
    return result;
}
} // namespace

int main()
{
    try
    {
        const auto direct = Run({});
        Check(direct.resumeWork == 60000 && direct.plannedCatchupWu == 5000, "direct did not use q=60 percent");
        auto options = Options{};
        options.name = "nonbusy-recompute-policy"; options.busyPolicy = RemoteBusyRecoveryPolicy::RECOMPUTE;
        const auto same = Run(options);
        Check(same.resumeWork == direct.resumeWork && same.acceptedNs == direct.acceptedNs &&
              same.computeStartedNs == direct.computeStartedNs && same.catchupNs == direct.catchupNs &&
              same.computedNs == direct.computedNs && same.actualRecoveryWu == direct.actualRecoveryWu,
              "busy policy changed a nonbusy recovery");
        options = {};
        options.name = "busy-relocate"; options.busy = true; options.expected = "RELOCATE";
        Run(options);
        options.name = "busy-recompute"; options.busyPolicy = RemoteBusyRecoveryPolicy::RECOMPUTE;
        options.expected = "RECOMPUTE"; options.deadline = 2.0;
        Run(options);
        options.name = "busy-recompute-deadline"; options.deadline = 1.3; options.success = false;
        Run(options);
        options = {}; options.name = "primary-f3"; options.primaryF3 = true; Run(options);
        options = {}; options.name = "local-result"; options.result = 0; Run(options);
        for (const auto& damage : {"recovery-f1", "recovery-f2", "recovery-f3", "result-f3"})
        {
            options = {}; options.name = damage; options.damage = damage;
            options.success = options.damage == "recovery-f1" || options.damage == "recovery-f2";
            Run(options);
        }
        options = {}; options.name = "relocation-source-f3"; options.damage = "source-f3";
        options.busy = true; options.expected = "RELOCATE"; options.success = false; Run(options);
        options = {}; options.name = "backup-unavailable"; options.damage = "backup-unavailable";
        options.expected = "RELOCATE"; options.busyPolicy = RemoteBusyRecoveryPolicy::RECOMPUTE; Run(options);
        options = {}; options.name = "backup-f3-before"; options.damage = "backup-f3-before";
        options.expected = "RECOMPUTE"; options.source = 2; options.deadline = 2.0; Run(options);
        for (const auto& damage : {"input-missing", "root-missing"})
        {
            options = {}; options.name = damage; options.damage = damage;
            options.merge40 = false; options.faultDelay = 150000000;
            if (options.damage == "root-missing") options.expected = "RECOMPUTE";
            Run(options);
        }
        options = {}; options.name = "no-checkpoint"; options.noCheckpoint = true;
        options.merge40 = false; options.expected = "RECOMPUTE"; options.deadline = 2.0; Run(options);
        options.name = "inclusive-compute-deadline"; options.deadline = 1.5; options.faultDelay = 499999998;
        const auto inclusive = Run(options);
        Check(inclusive.computedNs == inclusive.snapshot.deadlineNs,
              "controlled inclusive compute deadline not exercised");
        for (auto profile : {TaskProfile::DENSE_IMAGE, TaskProfile::SPARSE_INFERENCE, TaskProfile::COMPRESSION})
        {
            options = {}; options.name = TaskProfileToString(profile); options.profile = profile;
            options.merge40 = false; options.faultDelay = 500000000; Run(options);
        }
        std::cout << "CB-Sat recovery: " << checks << " invariant checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "CB-Sat recovery FAILED: " << error.what() << '\n';
        Simulator::Destroy();
        return 1;
    }
}
