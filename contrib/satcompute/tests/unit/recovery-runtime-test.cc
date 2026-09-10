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
        if (e.toState == TASK_RUNNING && protect)
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
    CheckpointManager manager(tasks, topology, o.capacity, end);
    Driver driver{manager, o.protect};
    tasks->ConnectTaskObserver(MakeCallback(&Driver::OnTask, &driver));
    FixedProtectionPolicy policy(50, 4);
    RecoveryController recovery(tasks, topology, manager, end, policy);
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
    const auto rows = recovery.Summaries();
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
    Check(rows.size() == 1, o.name + ": recovery row missing");
    const auto r = rows.front();
    std::cout << o.name << ": " << r.path << ' ' << r.terminalState << ' ' << r.reason
              << " x/l/r=" << r.snapshot.actualWork << '/' << r.snapshot.localWork << '/'
              << r.snapshot.remoteWork << " input=" << r.inputMode << " result=" << r.resultMode
              << '\n';
    Check(r.path == o.expectedPath, o.name + ": selected path differs");
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
                  (r.catchupRedoWork
                       ? ComputeService::CalculateServiceTimeNs(
                             r.catchupRedoWork, r.recoveryNode == 0 ? o.recoveryRate : 100000)
                       : 0),
              "catchup duration differs");
        Check(r.resultBytes == 4 && r.resultCompleteNs >= r.computeCompleteNs,
              "result byte/time contract differs");
        const auto terminals =
            std::count_if(tasks->GetTaskEvents().begin(),
                          tasks->GetTaskEvents().end(),
                          [](const auto& e) { return IsTerminalTaskState(e.toState); });
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
        if (plan.transferId > 2 &&
            !tasks->GetTransferEngine()->IsProtectionTransfer(plan.transferId))
        {
            Check(plan.sourceSatelliteId == *r.recoveryNode &&
                      plan.destinationSatelliteId == o.result && plan.sizeBytes == 4,
                  "winning RESULT used immutable primary source");
            Check(r.resultTransferId == plan.transferId, "result history lost runtime transfer ID");
            if (o.name == "network-result-f3")
                Check(tasks->GetTransferEngine()->GetTerminalReason(plan.transferId) ==
                          TransferTerminalReason::SOURCE_SATELLITE_FAILED,
                      "recovery RESULT did not retain ordinary F3 endpoint failure semantics");
        }
    }
    if (r.resultMode == "LOCAL")
        Check(r.resultTransferId == 0, "local result allocated UDP ID");
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
        Run({"tail-faster", {Fault(1, 3, 180000000)}, "TAIL"}, output);
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
             "RECOMPUTE",
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
