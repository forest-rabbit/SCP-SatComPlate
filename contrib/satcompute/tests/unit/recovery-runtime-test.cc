/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "../support/fault-injection.h"
#include "ns3/checkpoint-manager.h"
#include "ns3/command-line.h"
#include "ns3/fixed-protection-policy.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/local-delivery.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/protection-metrics.h"
#include "ns3/recovery-controller.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;

namespace ns3
{
/** Test-only stale callbacks cannot be fabricated through production CLI inputs. */
struct TaskCoordinatorRecoveryTestAccess
{
    static void Stale(Ptr<TaskCoordinator> tasks)
    {
        tasks->HandleComputeComplete(1, 3, Simulator::Now().GetNanoSeconds());
        tasks->HandleResultTransferComplete(2, Simulator::Now().GetNanoSeconds());
    }
};
} // namespace ns3

namespace
{
uint64_t checks{};

void
Check(bool value, const std::string& message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}

void
Reset()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

constexpr int64_t END = 2000000000;

FaultDefinition
Fault(uint64_t id, uint32_t node, int64_t at, bool permanent = false, bool f2 = false)
{
    FaultDefinition f;
    f.faultId = id;
    f.nodeId = node;
    f.startTimeNs = at;
    f.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
    if (!permanent)
    {
        f.durationNs = 200000000;
        f.failureProbability = 0.2;
    }
    f.f1Occurred = !permanent && !f2;
    f.f2Occurred = f2;
    return f;
}

/** Faults are test-injected, while all recovery work uses production services. */
struct Driver
{
    CheckpointManager& manager;
    bool protect{true};

    void OnTask(const TaskEventRecord& e)
    {
        if (e.toState == TASK_RUNNING && protect && e.taskId == 1)
        {
            ProtectionContext context;
            context.attempt = {e.taskId, 0};
            context.primaryNode = e.nodeId;
            context.nowNs = e.simulationTimeNs;
            manager.Execute(context,
                            {ActionKind::START_CHECKPOINT, CheckpointConfiguration{50, 4, 2, 0}});
        }
        if (e.toState == TASK_RESULT_TRANSFERRING && e.fromState == TASK_RUNNING)
            manager.OnTaskComputeComplete({e.taskId, 0});
        if (IsTerminalTaskState(e.toState))
            manager.OnTaskTerminal(e.taskId);
    }
};

struct Options
{
    std::string name;
    std::vector<FaultDefinition> faults;
    std::string expectedPath;
    bool success{true}, protect{true};
    uint32_t source{}, result{};
    uint64_t recoveryRate{100000}, capacity{2000000000};
    double deadlineFactor{1.3};
    bool lateFaultUid{};
    int64_t stopNs{END};
    bool remoteBusy{}, blockTargets{}, fillTargets{}, failState{};
    TaskProfile taskProfile{TaskProfile::LLM};
    RemoteBusyRecoveryPolicy busyPolicy{RemoteBusyRecoveryPolicy::RELOCATE};
    InputStagingPolicy inputPolicy{InputStagingPolicy::EAGER};
};

RecoverySummary
Run(const Options& o, const std::string& output)
{
    const auto end = o.stopNs;
    auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", end, end, 6171353);
    config.parameters.fixedDelaySeconds = 0.001;
    config.parameters.islBandwidthBps = 10000000000ULL;
    config.parameters.routingMode = "global-capacity-aware-hrw";
    OnlineTopologyController topology(config.parameters, config.constellation);
    topology.Initialize();
    auto tasks = CreateObject<TaskCoordinator>();
    ComputeProfile profile{{{0, o.recoveryRate}, {2, 100000}, {3, 100000}, {4, 100000}}};
    TaskTrace trace{{{1, o.source, 3, o.result, 400, 4, 100000, 1, 1, 2, TaskProfile::LLM}}};
    if (o.taskProfile != TaskProfile::LLM)
    {
        auto& task = trace.tasks.front();
        task.taskProfile = o.taskProfile;
        task.inputBytes = 52428800;
        task.computeWorkUnits = 78644;
    }
    if (o.remoteBusy)
        trace.tasks.push_back({2, 1, 0, 1, 400, 4, 100000, 450000000, 3, 4, TaskProfile::LLM});
    tasks->Initialize(profile,
                      trace,
                      topology,
                      "size-aware",
                      1024,
                      config.parameters.islMtuBytes,
                      config.parameters.receiverRcvBufBytes,
                      false,
                      end,
                      o.deadlineFactor);
    auto fault = CreateObject<FaultController>();
    std::vector<uint32_t> ids;
    for (uint32_t id = 0; id < 16; ++id)
        ids.push_back(id);
    if (!o.lateFaultUid)
        FaultControllerTestAccess::Schedule(fault, o.faults, ids, end);
    else
    {
        fault->ConfigureGeneration(ids, end);
        for (auto f : o.faults)
            Simulator::Schedule(NanoSeconds(*f.startTimeNs - 1), [fault, f] {
                Simulator::Schedule(NanoSeconds(1), [fault, f] {
                    fault->SubmitGeneratedBatch({{FaultEventType::START, f}});
                });
            });
    }
    fault->BindTopology(topology);
    fault->BindTaskCoordinator(tasks);
    CheckpointManager manager(tasks, topology, o.capacity, end, o.inputPolicy);
    Driver driver{manager, o.protect};
    tasks->ConnectTaskObserver(MakeCallback(&Driver::OnTask, &driver));
    FixedProtectionPolicy policy(50, 4);
    RecoveryController recovery(tasks, topology, manager, end, policy, o.busyPolicy);
    if (o.blockTargets || o.fillTargets)
    {
        Simulator::Schedule(NanoSeconds(480000000), [&] {
            for (auto node : {2u, 4u})
            {
                if (o.fillTargets)
                    Check(manager.Pool(node)
                              .Allocate(99, StorageKind::REMOTE_STATE, manager.Pool(node).Free())
                              .has_value(),
                          "target fill failed");
                if (o.blockTargets)
                    for (auto service : tasks->GetComputeServices())
                        if (service->GetNodeId() == node)
                            Check(service->ReserveRecovery(99, 1), "target lock failed");
            }
        });
        Simulator::Schedule(NanoSeconds(1100000000), [&] {
            for (auto node : {2u, 4u})
            {
                manager.Pool(node).ReleaseTask(99);
                for (auto service : tasks->GetComputeServices())
                    if (service->GetNodeId() == node)
                        service->CancelRecovery(99, 1);
            }
        });
    }
    if (o.failState)
        Simulator::Schedule(NanoSeconds(500100000), [&] {
            const auto r = recovery.Summaries().front();
            Check(manager.Pool(r.snapshot.remoteNode).Find(r.snapshot.remoteObject) != nullptr,
                  "migration released only checkpoint before destination received it");
            bool failed = false;
            for (const auto& flow : manager.Flows())
                if (flow.key.kind == ProtectionTransferKind::RECOVERY_STATE)
                    failed = tasks->GetTransferEngine()->FinalizeTransferIfActive(
                        flow.transferId,
                        TransferTerminalState::FAILED,
                        TransferTerminalReason::TASK_FAILED);
            Check(failed, "migration failure fixture missed real state flow");
        });
    if (!o.faults.empty())
        Simulator::Schedule(NanoSeconds(600000000), [tasks] {
            const auto& task = tasks->GetTaskRuntimes().front();
            if (task.attemptGeneration == 1)
            {
                const auto before = task.state;
                TaskCoordinatorRecoveryTestAccess::Stale(tasks);
                Check(task.state == before,
                      "stale primary compute/result callback mutated recovery");
            }
        });
    Simulator::Stop(NanoSeconds(end));
    Simulator::Run();
    recovery.Finalize();
    manager.Finalize();
    recovery.WriteMetrics(std::filesystem::path(output) / o.name);
    WriteProtectionMetrics(
        manager, *tasks->GetTransferEngine(), std::filesystem::path(output) / o.name);
    const auto rows = recovery.Summaries();
    const bool deferred = o.inputPolicy == InputStagingPolicy::DEFERRED;
    if (deferred)
    {
        Check(std::none_of(manager.Flows().begin(), manager.Flows().end(), [](const auto& f) {
            return f.key.kind == ProtectionTransferKind::INIT_BASE;
        }), "deferred sent normal-period original INPUT");
        for (const auto& e : manager.Events())
            if (e.event == "INIT_STATE_IDENTITY_READY")
                Check(e.bytes == 0 && e.storageObject != 0, "deferred zero-state lost logical identity");
        for (const auto& p : manager.Summaries())
            if (p.initializationNs >= 0)
                Check(p.initializationNs == p.startNs + p.localCostNs + p.remoteCostNs,
                      "zero-progress deferred initialization omitted cL/cR or waited for INPUT");
    }
    if (o.faults.empty())
    {
        RecoverySummary probe;
        uint32_t commits = 0;
        for (const auto& event : manager.Events())
            if (event.event == "REMOTE_COMMIT" && ++commits == 2)
            {
                probe.snapshot.faultNs = event.timeNs;
                break;
            }
        Check(probe.snapshot.faultNs > 0, "probe did not commit a remote batch");
        Reset();
        return probe;
    }
    // F3 of busy remote also interrupts its ordinary background task.
    Check(rows.size() == (o.name == "migrate-f3-0" ? 2u : 1u), o.name + ": recovery row missing");
    const auto r = rows.front();
    if (o.name == "remote-f3-recompute")
        Check(r.checkpointFallbackReason == "REMOTE_F3", "remote F3 fallback diagnostic");
    if (o.name == "remote-compute-outage")
        Check(r.relocationTrigger == "REMOTE_UNAVAILABLE" && r.relocationAttempted,
              "compute outage incorrectly destroyed readable checkpoint");
    if (r.path.starts_with("MIGRATE_"))
    {
        const TaskStateAdapter layout(trace.tasks.front());
        Check(r.checkpointStateExists && r.relocationAttempted &&
                  r.recoveryNode != r.snapshot.remoteNode &&
                  r.relocationBytes == layout.CommittedStateBytes(r.snapshot.remoteWork, o.inputPolicy),
              "migration did not preserve exact checkpoint sizing/ownership");
        Check(deferred ? r.inputStartedNs >= 0 : r.inputStartedNs < 0, "migration INPUT policy mismatch");
        if (o.remoteBusy)
            Check(r.remoteBusyAtFault && r.relocationTrigger == "REMOTE_BUSY",
                  "busy fixture missed busy");
        if (o.success)
        {
            Check(r.stateReceivedNs > r.acceptedNs && r.computeStartedNs >= r.stateReceivedNs,
                  "migration computed before actual state reception");
            if (r.path == "MIGRATE_TAIL")
                Check(r.tailCommitNs ==
                          std::max(r.tailReceivedNs, r.stateReceivedNs) + r.snapshot.remoteCostNs,
                      "parallel state/tail did not wait for both receivers plus cR");
        }
    }
    if (o.name == "off-recompute-local" || o.name == "initializing-recompute")
        Check(!r.checkpointStateExists && r.checkpointFallbackReason == "STATE_MISSING",
              "missing checkpoint fallback diagnostic");
    if (r.path == "TAIL" || r.path == "REMOTE_REDO")
        Check(r.checkpointStateExists && r.checkpointFallbackReason.empty(),
              "checkpoint recovery incorrectly labeled fallback");
    if (o.name == "tail-faster" || o.name == "recovery-f3-fails")
        // 250 complete tokens put costs in tier 1; legal captures end at
        // 5600/10800/16000 WU. Fault progress 17699 leaves exactly 1699 WU to redo.
        Check(r.normalProtectionCostNs == 900000 && r.reservedIdleNs == 6190259 &&
                  r.plannedCatchupRedoWu == 1699 && r.plannedTotalRecoveryWu == 84000 &&
                  r.actualCatchupRedoWu == 1699 &&
                  r.actualTotalRecoveryWu == (o.success ? 84000 : 6380) &&
                  r.actualPostCatchupWu == (o.success ? 82301 : 4681),
              "manual TAIL success/failure accounting anchor differs");
    if (o.name == "off-recompute-local")
        Check(r.normalProtectionCostNs == 0 && r.reservedIdleNs == 1 &&
                  r.plannedCatchupRedoWu == 7699 && r.actualTotalRecoveryWu == 100000 &&
                  r.actualCatchupRedoWu == 7699 && r.actualPostCatchupWu == 92301,
              "manual RECOMPUTE accounting anchor differs");
    Check(manager.IsQuiescent(), "final protection requests/merges/runtime transfers leaked");
    Check(r.actualTotalRecoveryWu == r.actualCatchupRedoWu + r.actualPostCatchupWu,
          "actual recovery partition differs");
    Check(r.plannedTotalRecoveryWu == r.plannedCatchupRedoWu + r.plannedPostCatchupWu,
          "planned recovery partition differs");
    Check(r.actualTotalRecoveryWu <= r.plannedTotalRecoveryWu, "actual exceeds planned work");
    Check(r.actualTotalRecoveryWu ==
              std::min<uint64_t>(r.plannedTotalRecoveryWu,
                                 static_cast<unsigned __int128>(r.actualServiceNs) *
                                     r.recoveryRate / 1000000000),
          "actual WU does not follow real integer service");
    if (r.computeCompleteNs >= 0)
        Check(r.actualTotalRecoveryWu == r.plannedTotalRecoveryWu,
              "completed recovery work missing");
    if (r.catchupNs >= 0)
        Check(r.actualCatchupRedoWu == r.plannedCatchupRedoWu, "real catchup work missing");
    else
        Check(r.actualPostCatchupWu == 0, "post-catchup work before catchup");
    if (r.acceptedNs >= 0)
        Check(r.reservedIdleNs ==
                  (r.computeStartedNs >= 0 ? r.computeStartedNs : r.terminalNs) - r.acceptedNs,
              "reserved idle includes extra cR or omits never-started wait");
    for (const auto& summary : manager.Summaries())
    {
        uint64_t local = 0, remote = 0;
        for (const auto& event : manager.Events())
            if (event.taskId == summary.taskId && event.generation == 0)
            {
                local += event.event == "INIT_STATE_GENERATED" || event.event == "L1_GENERATED";
                remote +=
                    event.event == "INIT_COST_COMMITTED" || event.event == "REMOTE_COST_COMMITTED";
            }
        Check(summary.normalCostNs == local * summary.localCostNs + remote * summary.remoteCostNs,
              "normal cost differs from real event counts");
        Check(summary.normalCostNs == r.normalProtectionCostNs,
              "recovery duplicates/omits normal cost");
    }
    std::cout << o.name << ": " << r.path << ' ' << r.terminalState << ' ' << r.reason
              << " x/l/r=" << r.snapshot.actualWork << '/' << r.snapshot.localWork << '/'
              << r.snapshot.remoteWork << " input=" << r.inputMode << " result=" << r.resultMode
              << '\n';
    Check(o.expectedPath == "MIGRATE" ? r.path.starts_with("MIGRATE_") : r.path == o.expectedPath,
          o.name + ": selected path differs");
    Check(tasks->GetTaskRuntimes().front().TaskSucceeded() == o.success,
          o.name + ": logical outcome differs");
    Check(r.snapshot.remoteWork <= r.snapshot.localWork &&
              r.snapshot.localWork <= r.snapshot.actualWork,
          "snapshot ordering violated");
    if (o.success)
    {
        tasks->ValidateCompleted();
        Check(r.catchupNs >= r.computeStartedNs && r.computeCompleteNs >= r.catchupNs,
              "catchup must be a real intermediate compute milestone");
        Check(r.catchupNs - r.computeStartedNs ==
                  (r.plannedCatchupRedoWu
                       ? ComputeService::CalculateServiceTimeNs(
                             r.plannedCatchupRedoWu, r.recoveryNode == 0 ? o.recoveryRate : 100000)
                       : 0),
              "catchup duration differs");
        Check(r.resultBytes == 4 && r.resultCompleteNs >= r.computeCompleteNs,
              "result byte/time contract differs");
        const auto terminals = std::count_if(
            tasks->GetTaskEvents().begin(),
            tasks->GetTaskEvents().end(),
            [](const auto& e) { return e.taskId == 1 && IsTerminalTaskState(e.toState); });
        Check(terminals == 1, "logical task terminalized more than once");
    }
    for (const auto& [node, pool] : manager.Pools())
        Check(pool->Used() == 0 && pool->Reserved() == 0, "recovery storage leaked");
    for (auto service : tasks->GetComputeServices())
    {
        Check(!service->HasRecoveryReservation(), "recovery slot leaked");
        Check(service->GetBusyTimeNs() <= static_cast<uint64_t>(end),
              "post-Run cleanup double-counted busy time");
    }
    for (const auto& plan : tasks->GetTransferEngine()->GetPlans())
    {
        Check(plan.sourceSatelliteId != plan.destinationSatelliteId,
              "same-node synthetic UDP created");
        if (tasks->GetTransferEngine()->IsRuntimeTransfer(plan.transferId) &&
            !tasks->GetTransferEngine()->IsProtectionTransfer(plan.transferId))
        {
            const auto owner = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
                return row.resultTransferId == plan.transferId;
            });
            Check(owner != rows.end(), "winning RESULT has no recovery owner");
            const auto& definitions = tasks->GetTaskRuntimes();
            const auto original = std::find_if(definitions.begin(), definitions.end(), [&](const auto& t) {
                return t.definition.taskId == owner->snapshot.taskId;
            });
            Check(original != definitions.end() && plan.sourceSatelliteId == *owner->recoveryNode &&
                      plan.destinationSatelliteId == original->definition.resultNodeId &&
                      plan.sizeBytes == original->definition.outputBytes,
                  "winning RESULT used immutable primary source");
            if (o.name == "network-result-f3")
                Check(tasks->GetTransferEngine()->GetTerminalReason(plan.transferId) ==
                          TransferTerminalReason::SOURCE_SATELLITE_FAILED,
                      "recovery RESULT did not retain ordinary F3 endpoint failure semantics");
        }
    }
    if (r.resultMode == "LOCAL")
        Check(r.resultTransferId == 0, "local result allocated UDP ID");
    if (deferred && r.acceptedNs >= 0)
    {
        uint64_t inputs = 0, merges = 0;
        for (const auto& e : recovery.Events())
            if (e.taskId == 1)
            {
                if (e.event == "RECOVERY_INPUT_STARTED")
                {
                    ++inputs;
                    Check(e.bytes == trace.tasks.front().inputBytes && e.timeNs == r.acceptedNs,
                          "fault INPUT is not one full original input requested immediately");
                }
                merges += e.event == "RECOVERY_TAIL_COMMIT";
            }
        Check(inputs == 1 && merges <= 1, "deferred duplicated INPUT or fault cR");
        if (r.tailStartedNs >= 0)
            Check(r.tailStartedNs == r.inputStartedNs, "independent INPUT/tail artificially serialized");
        if (r.stateStartedNs >= 0)
            Check(r.stateStartedNs == r.inputStartedNs, "independent INPUT/state artificially serialized");
        if (r.computeStartedNs >= 0)
            Check(r.inputReceivedNs >= 0 && r.stateReadyNs >= 0 &&
                      r.computeStartedNs == std::max(r.inputReceivedNs, r.stateReadyNs),
                  "deferred compute does not follow exact dependency join");
        if (r.path == "REMOTE_REDO" || r.path == "MIGRATE_REDO" || r.path == "RECOMPUTE")
            Check(merges == 0 && r.tailCommitNs < 0, "committed state copy or INPUT paid fault cR");
        uint64_t maxNode = 0, sumNode = 0;
        for (const auto& [node, pool] : manager.Pools())
        {
            maxNode = std::max(maxNode, pool->PeakTotal());
            sumNode += pool->PeakTotal();
        }
        Check(manager.GlobalStoragePeakBytes() >= maxNode && manager.GlobalStoragePeakBytes() <= sumNode,
              "global simultaneous storage peak violates per-node bounds");
    }
    const auto capacity = tasks->GetTransferEngine()->CollectCapacityAwareSummary();
    Check(capacity.activePathCountAtEnd == 0 && capacity.totalReservedRateBpsAtEnd == 0,
          "recovery capacity reservation leaked");
    tasks->DisconnectTaskObserver(MakeCallback(&Driver::OnTask, &driver));
    Reset();
    return r;
}

void
Local()
{
    uint64_t calls = 0;
    LocalDelivery::Schedule(
        7,
        7,
        400,
        [] { return true; },
        [&](uint64_t bytes, int64_t at) {
            Check(bytes == 400 && at == 1, "local logical payload or phase boundary differs");
            ++calls;
        });
    LocalDelivery::Schedule(
        7, 7, 4, [] { return false; }, [&](uint64_t, int64_t) { ++calls; });
    auto cancelled = LocalDelivery::Schedule(
        7, 7, 4, [] { return true; }, [&](uint64_t, int64_t) { ++calls; });
    Simulator::Cancel(cancelled);
    bool rejected = false;
    try
    {
        LocalDelivery::Schedule(
            7, 8, 4, [] { return true; }, [](uint64_t, int64_t) {});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Simulator::Run();
    Check(calls == 1 && rejected, "local guard/cancellation/cross-node rejection failed");
    Reset();
}

/** Reserved-idle is not compute busy, and temporary outage immunity is attempt-scoped. */
struct ServiceRecorder
{
    int64_t ordinaryStart{-1}, ordinaryEnd{-1}, recoveryStart{-1}, catchup{-1}, recoveryEnd{-1};

    void Start(uint64_t, uint32_t, int64_t at)
    {
        ordinaryStart = at;
    }

    void End(uint64_t, uint32_t, int64_t at)
    {
        ordinaryEnd = at;
    }

    void RecoveryStart(uint64_t, uint64_t generation, uint32_t, int64_t at)
    {
        Check(generation == 1, "service lost recovery generation");
        recoveryStart = at;
    }

    void Catchup(uint64_t, uint64_t, uint32_t, int64_t at)
    {
        catchup = at;
    }

    void RecoveryEnd(uint64_t, uint64_t, uint32_t, int64_t at)
    {
        recoveryEnd = at;
    }
};

void
Reservation(bool cancel)
{
    auto node = CreateObject<Node>();
    auto service = CreateObject<ComputeService>();
    ServiceRecorder recorder;
    service->Configure(7,
                       1000000000,
                       MakeCallback(&ServiceRecorder::Start, &recorder),
                       MakeCallback(&ServiceRecorder::End, &recorder));
    node->AddApplication(service);
    service->SetStartTime(NanoSeconds(0));
    service->SetStopTime(NanoSeconds(30));
    Simulator::Schedule(NanoSeconds(1), [service] {
        Check(service->ReserveRecovery(1, 1), "idle recovery reservation rejected");
        Check(!service->ReserveRecovery(3, 1) && !service->IsIdle(),
              "reservation allowed a second owner");
    });
    Simulator::Schedule(NanoSeconds(2), [service] {
        service->SubmitTask(2, 3, Simulator::Now().GetNanoSeconds());
        Check(!service->HasRunningTask() && service->GetBusyTimeNs() == 0,
              "reserved-idle consumed compute or dispatched ordinary queue");
    });
    Simulator::Schedule(NanoSeconds(3), [service, &recorder] {
        service->SetComputeAvailable(false);
        Check(service->StartRecovery(1,
                                     1,
                                     10,
                                     4,
                                     MakeCallback(&ServiceRecorder::RecoveryStart, &recorder),
                                     MakeCallback(&ServiceRecorder::Catchup, &recorder),
                                     MakeCallback(&ServiceRecorder::RecoveryEnd, &recorder)),
              "accepted attempt could not run during compute outage");
        Check(!service->CancelRecovery(1, 0) && !service->ReleaseRecovery(2, 1),
              "stale attempt released recovery service");
    });
    if (cancel)
        Simulator::Schedule(NanoSeconds(6), [service] {
            Check(service->CancelRecovery(1, 1), "F3 cancellation lost recovery owner");
        });
    Simulator::Schedule(NanoSeconds(15), [service, &recorder] {
        Check(!service->HasRecoveryReservation() && !service->IsComputeAvailable() &&
                  service->GetQueueSize() == 1 && recorder.ordinaryStart < 0,
              "recovery cleared global outage or granted queued task immunity");
    });
    Simulator::Schedule(NanoSeconds(20), [service] { service->SetComputeAvailable(true); });
    Simulator::Stop(NanoSeconds(30));
    Simulator::Run();
    Check(recorder.recoveryStart == 3 && recorder.ordinaryStart == 20 && recorder.ordinaryEnd == 23,
          "reserved service/ordinary FCFS timing differs");
    Check(cancel ? recorder.catchup < 0 && recorder.recoveryEnd < 0 && service->GetBusyTimeNs() == 6
                 : recorder.catchup == 7 && recorder.recoveryEnd == 13 &&
                       service->GetBusyTimeNs() == 13,
          "recovery catchup/cancellation actual service accounting differs");
    Reset();
}
} // namespace

int
main(int argc, char** argv)
{
    std::string output = "output/n5a-g3/controlled";
    CommandLine cli;
    cli.AddValue("outputDir", "Controlled recovery evidence", output);
    cli.Parse(argc, argv);
    try
    {
        Local();
        Reservation(false);
        Reservation(true);
        auto noBase =
            Run({"off-recompute-local", {Fault(1, 3, 80000000)}, "RECOMPUTE", true, false}, output);
        Check(noBase.inputMode == "LOCAL" && noBase.resultMode == "LOCAL",
              "same-node recovery not selected");
        Options truncated{"simulation-cutoff", {Fault(1, 3, 80000000)}, "RECOMPUTE", false, false};
        truncated.stopNs = 500000000;
        auto cutoff = Run(truncated, output);
        Check(cutoff.reason == "SIMULATION_ENDED", "truncated recovery did not cleanly finalize");
        Options equality{"deadline-equality", {Fault(1, 3, 80000000)}, "RECOMPUTE", true, false};
        equality.deadlineFactor = static_cast<double>(noBase.computeCompleteNs -
                                                      (noBase.snapshot.deadlineNs - 1300000000)) /
                                  1000000000.0;
        auto equal = Run(equality, output);
        Check(equal.computeCompleteNs == equal.snapshot.deadlineNs,
              "inclusive deadline fixture missed equality");
        auto init = Run({"initializing-recompute", {Fault(1, 3, 4000000)}, "RECOMPUTE"}, output);
        Check(init.snapshot.phase == "INITIALIZING", "fixture did not hit initialization");
        Run({"empty-tail-redo", {Fault(1, 3, 35000000)}, "REMOTE_REDO"}, output);
        auto tail = Run({"tail-faster", {Fault(1, 3, 180000000)}, "TAIL"}, output);
        auto pre = Run({"f3-before-catchup",
                        {Fault(1, 3, 180000000), Fault(2, 0, tail.computeStartedNs + 1, true)},
                        "TAIL",
                        false},
                       output);
        Check(pre.actualCatchupRedoWu < pre.plannedCatchupRedoWu && pre.actualPostCatchupWu == 0,
              "pre-catchup F3 charged unexecuted work");
        auto wait = Run({"f3-reserved-idle",
                         {Fault(1, 3, 180000000), Fault(2, 0, tail.acceptedNs + 100, true)},
                         "TAIL",
                         false},
                        output);
        Check(wait.computeStartedNs < 0 && wait.actualTotalRecoveryWu == 0 &&
                  wait.reservedIdleNs == 100,
              "never-started recovery accounting differs");
        Run({"redo-faster", {Fault(1, 3, 180000000)}, "REMOTE_REDO", true, true, 0, 0, 10000000},
            output);
        Run({"local-f3-redo",
             {Fault(1, 2, 170000000, true), Fault(2, 3, 180000000)},
             "REMOTE_REDO"},
            output);
        Run({"remote-f3-recompute",
             {Fault(1, 0, 170000000, true), Fault(2, 3, 180000000)},
             "RECOMPUTE",
             true,
             true,
             1,
             1},
            output);
        Run({"remote-compute-outage",
             {Fault(1, 0, 170000000), Fault(2, 3, 180000000)},
             "MIGRATE_TAIL",
             true,
             true,
             1,
             1},
            output);
        Run({"source-f3-no-input",
             {Fault(1, 1, 70000000, true), Fault(2, 3, 80000000)},
             "RECOMPUTE",
             false,
             false,
             1,
             0},
            output);
        Run({"recovery-f1-immune", {Fault(1, 3, 180000000), Fault(2, 0, 250000000)}, "TAIL"},
            output);
        Run({"recovery-f2-immune",
             {Fault(1, 3, 180000000), Fault(2, 0, 250000000, false, true)},
             "TAIL"},
            output);
        Run({"recovery-f3-fails",
             {Fault(1, 3, 180000000), Fault(2, 0, 250000000, true)},
             "TAIL",
             false},
            output);
        Run({"deadline-fails",
             {Fault(1, 3, 80000000)},
             "RECOMPUTE",
             false,
             false,
             0,
             0,
             100000,
             2000000000,
             1.0},
            output);
        auto netResult =
            Run({"network-result", {Fault(1, 3, 180000000)}, "TAIL", true, true, 0, 1}, output);
        Run({"network-result-f3",
             {Fault(1, 3, 180000000), Fault(2, 0, netResult.computeCompleteNs + 500000, true)},
             "TAIL",
             false,
             true,
             0,
             1},
            output);
        Run({"primary-f3-tail", {Fault(1, 3, 180000000, true)}, "TAIL"}, output);
        const auto probe = Run({"commit-probe", {}, ""}, output);
        Options same{"same-ns-fault-first", {Fault(1, 3, probe.snapshot.faultNs)}, "TAIL"};
        const auto first = Run(same, output);
        same.name = "same-ns-commit-first";
        same.lateFaultUid = true;
        const auto second = Run(same, output);
        Check(first.snapshot.remoteWork > 0 && second.snapshot.remoteWork == first.snapshot.remoteWork &&
                  first.snapshot.remoteBytes > 0 &&
                  first.snapshot.remoteBytes == second.snapshot.remoteBytes &&
                  first.snapshot.localObjects == second.snapshot.localObjects &&
                  first.snapshot.tailBytes == second.snapshot.tailBytes &&
                  first.resultCompleteNs == second.resultCompleteNs &&
                  first.normalProtectionCostNs == second.normalProtectionCostNs,
              "same-ns UID reversal changed physical snapshot or recovery result");
        for (auto profile : {TaskProfile::LLM,
                             TaskProfile::DENSE_IMAGE,
                             TaskProfile::SPARSE_INFERENCE,
                             TaskProfile::COMPRESSION})
        {
            Options migration{std::string("migrate-tail-") + TaskProfileToString(profile),
                              {Fault(1, 3, 500000000)},
                              "MIGRATE"};
            migration.remoteBusy = true;
            migration.taskProfile = profile;
            Run(migration, output);
        }
        Options busyRecompute{"busy-policy-recompute", {Fault(1, 3, 500000000)}, "RECOMPUTE"};
        busyRecompute.remoteBusy = true;
        busyRecompute.deadlineFactor = 3;
        busyRecompute.busyPolicy = RemoteBusyRecoveryPolicy::RECOMPUTE;
        const auto recomputed = Run(busyRecompute, output);
        Check(recomputed.checkpointFallbackReason == "REMOTE_BUSY" &&
                  !recomputed.relocationAttempted && recomputed.inputStartedNs >= 0 &&
                  recomputed.plannedCatchupRedoWu == recomputed.snapshot.actualWork &&
                  recomputed.actualCatchupRedoWu == recomputed.plannedCatchupRedoWu,
              "busy recompute did not replay INPUT and execute full catch-up");
        busyRecompute.name = "busy-policy-recompute-interrupted";
        busyRecompute.success = false;
        busyRecompute.faults.push_back(Fault(2, *recomputed.recoveryNode,
                                           recomputed.computeStartedNs + 1000000, true));
        const auto interrupted = Run(busyRecompute, output);
        Check(interrupted.actualCatchupRedoWu > 0 &&
                  interrupted.actualCatchupRedoWu < interrupted.plannedCatchupRedoWu &&
                  interrupted.actualPostCatchupWu == 0,
              "interrupted recompute charged planned rather than executed catch-up");
        Options unavailable{"remote-compute-outage",
                            {Fault(1, 0, 170000000), Fault(2, 3, 180000000)},
                            "MIGRATE_TAIL", true, true, 1, 1};
        unavailable.busyPolicy = RemoteBusyRecoveryPolicy::RECOMPUTE;
        Run(unavailable, output);
        for (const auto* reason : {"REMOTE_UNAVAILABLE", "PATH_UNAVAILABLE", "REMOTE_F3", "STATE_MISSING"})
            Check(AllowsCheckpointRelocation(RemoteBusyRecoveryPolicy::RECOMPUTE, reason),
                  "busy policy changed a non-busy branch");
        Options redoMigration{"migrate-redo",
                              {Fault(1, 2, 490000000, true), Fault(2, 3, 500000000)},
                              "MIGRATE_REDO"};
        redoMigration.remoteBusy = true;
        Run(redoMigration, output);
        Options blockedMigration{"migrate-no-target", {Fault(1, 3, 500000000)}, "RECOMPUTE", false};
        blockedMigration.remoteBusy = blockedMigration.blockTargets = true;
        auto blocked = Run(blockedMigration, output);
        Check(blocked.relocationAttempted &&
                  blocked.relocationFailureReason == "NO_ELIGIBLE_RECOVERY_NODE",
              "missing relocation target not diagnosed");
        blockedMigration.name = "migrate-no-storage";
        blockedMigration.blockTargets = false;
        blockedMigration.fillTargets = true;
        auto full = Run(blockedMigration, output);
        Check(full.relocationFailureReason == "DESTINATION_STORAGE_UNAVAILABLE",
              "destination storage rejection not diagnosed");
        Options failedMigration{"migrate-transfer-failed",
                                {Fault(1, 3, 500000000)},
                                "MIGRATE_TAIL",
                                false};
        failedMigration.remoteBusy = failedMigration.failState = true;
        auto failed = Run(failedMigration, output);
        Check(!failed.relocationFailureReason.empty() && failed.actualTotalRecoveryWu == 0,
              "failed migration lost failure accounting");
        for (auto node : {0u, 2u})
        {
            Options lost{std::string("migrate-f3-") + std::to_string(node),
                         {Fault(1, 3, 500000000), Fault(2, node, 500100000, true)},
                         "MIGRATE_TAIL",
                         false};
            lost.remoteBusy = true;
            auto terminal = Run(lost, output);
            Check(terminal.actualTotalRecoveryWu == 0 && !terminal.relocationFailureReason.empty(),
                  "F3 during migration started compute or lost terminal diagnostic");
        }
        Options deferred{"deferred-zero-redo", {Fault(1, 3, 35000000)}, "REMOTE_REDO"};
        deferred.inputPolicy = InputStagingPolicy::DEFERRED;
        const auto zero = Run(deferred, output);
        Check(zero.snapshot.phase == "ON" && zero.snapshot.remoteObject && zero.snapshot.remoteBytes == 0 &&
                  zero.inputMode == "LOCAL" && zero.snapshot.remoteWork == 0,
              "committed zero state treated as missing or local INPUT used UDP");
        deferred.name = "deferred-tail-input-first";
        deferred.faults = {Fault(1, 3, 180000000)};
        deferred.expectedPath = "TAIL";
        deferred.source = 7; // Separate incoming link: small INPUT arrives before the KV tail.
        const auto joined = Run(deferred, output);
        Check(joined.inputReceivedNs < joined.stateReadyNs && joined.inputMode == "NETWORK",
              "fixture missed INPUT-first tail dependency");
        deferred.name = "deferred-source-f3-pending";
        deferred.success = false;
        deferred.faults.push_back(Fault(2, deferred.source, joined.inputStartedNs + 100, true));
        const auto lost = Run(deferred, output);
        Check(lost.inputReceivedNs < 0 && lost.actualTotalRecoveryWu == 0 &&
                  lost.reason == "RECOVERY_F3_SATELLITE_FAILURE", "pending original INPUT ignored source F3");
        deferred.name = "deferred-source-f3-after-input";
        deferred.success = true;
        deferred.faults.back() = Fault(2, deferred.source, joined.inputReceivedNs + 1, true);
        Run(deferred, output);
        for (auto profile : {TaskProfile::LLM, TaskProfile::DENSE_IMAGE,
                             TaskProfile::SPARSE_INFERENCE, TaskProfile::COMPRESSION})
        {
            Options move{std::string("deferred-migrate-") + TaskProfileToString(profile),
                         {Fault(1, 3, 500000000)}, "MIGRATE"};
            move.inputPolicy = InputStagingPolicy::DEFERRED;
            move.taskProfile = profile;
            // Opposite incoming routes let the small sparse state finish before INPUT;
            // a shared link can legitimately serialize them through normal admission.
            move.source = profile == TaskProfile::SPARSE_INFERENCE ? 4 : 1;
            move.remoteBusy = true;
            auto moved = Run(move, output);
            if (profile == TaskProfile::SPARSE_INFERENCE)
                Check(moved.stateReadyNs < moved.inputReceivedNs,
                      "fixture missed state-first original INPUT dependency");
            move.name += "-recompute";
            move.busyPolicy = RemoteBusyRecoveryPolicy::RECOMPUTE;
            move.expectedPath = "RECOMPUTE";
            move.deadlineFactor = 3;
            Run(move, output);
        }
        redoMigration.name = "deferred-migrate-redo";
        redoMigration.inputPolicy = InputStagingPolicy::DEFERRED;
        Run(redoMigration, output);
        failedMigration.name = "deferred-state-transfer-failed";
        failedMigration.inputPolicy = InputStagingPolicy::DEFERRED;
        Run(failedMigration, output);
        std::cout << "recovery-runtime-test: PASS (" << checks << " checks)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "recovery-runtime-test: " << error.what() << '\n';
        Reset();
        return 1;
    }
}
