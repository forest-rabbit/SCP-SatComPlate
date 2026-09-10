/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "ns3/checkpoint-manager.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/placement-policy.h"
#include "ns3/protection-transfer-key.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;

namespace
{
uint64_t checks{};

void
Check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

void
Reset()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

constexpr int64_t END = 1300000000;

auto
Configuration(const std::string& routing = "global-capacity-aware-hrw")
{
    auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
    config.parameters.fixedDelaySeconds = 0.001;
    config.parameters.islBandwidthBps = 10000000000ULL;
    config.parameters.routingMode = routing;
    return config;
}

TaskDefinition
Llm(uint64_t id, uint32_t node = 3)
{
    return {id, 0, node, 0, 400, 4, 100000, 1, 2 * id - 1, 2 * id, TaskProfile::LLM};
}

Ptr<TaskCoordinator>
Tasks(OnlineTopologyController& topology, const SatComputeConfig& config, bool two = false)
{
    auto tasks = CreateObject<TaskCoordinator>();
    ComputeProfile profile{{{0, 100000}, {2, 100000}, {3, 100000}, {4, 100000}}};
    TaskTrace trace{{Llm(1)}};
    if (two)
        trace.tasks.push_back(Llm(2, 4));
    tasks->Initialize(profile,
                      trace,
                      topology,
                      "size-aware",
                      1024,
                      config.islMtuBytes,
                      config.receiverRcvBufBytes,
                      false,
                      END);
    return tasks;
}

/** Controlled events operate only the protection mechanism, never invent fault/recovery paths. */
class Driver
{
  public:
    CheckpointManager& manager;
    Ptr<TaskCoordinator> tasks;
    std::string stopAt;
    bool stopped{}, delayed{}, sameNs{};
    size_t cursor{};

    explicit Driver(CheckpointManager& m, Ptr<TaskCoordinator> t) : manager(m), tasks(t)
    {
    }

    void Start(uint64_t id, uint32_t node)
    {
        ProtectionContext context;
        context.attempt = {id, 0};
        context.primaryNode = node;
        context.nowNs = Simulator::Now().GetNanoSeconds();
        manager.Execute(context,
                        {ActionKind::START_CHECKPOINT, CheckpointConfiguration{50, 4, 2, 0}});
    }

    void Task(const TaskEventRecord& event)
    {
        if (event.toState == TASK_RUNNING)
        {
            if (sameNs)
                Simulator::Schedule(
                    NanoSeconds(999500000), &Driver::Start, this, event.taskId, event.nodeId);
            else if (delayed)
                Simulator::Schedule(
                    NanoSeconds(20000000), &Driver::Start, this, event.taskId, event.nodeId);
            else
                Start(event.taskId, event.nodeId);
        }
        if (event.toState == TASK_RESULT_TRANSFERRING)
            manager.OnTaskComputeComplete({event.taskId, 0});
        if (IsTerminalTaskState(event.toState))
            manager.OnTaskTerminal(event.taskId);
    }

    void Poll()
    {
        if (stopped)
            return;
        // Inspect actual production events/flows; stop within the requested live phase.
        for (; cursor < manager.Events().size(); ++cursor)
        {
            const auto event = manager.Events()[cursor];
            if (event.event == stopAt)
            {
                stopped = true;
                manager.OnTaskTerminal(event.taskId);
                manager.OnTaskTerminal(event.taskId); // Idempotent repeated cleanup.
                return;
            }
        }
        if (stopAt == "L1_IN_FLIGHT" || stopAt == "BATCH_IN_FLIGHT" || stopAt == "FAIL_L1")
        {
            for (const auto& flow : manager.Flows())
            {
                const auto kind = stopAt == "BATCH_IN_FLIGHT" ? ProtectionTransferKind::REMOTE_BATCH
                                                              : ProtectionTransferKind::L1;
                if (flow.key.kind != kind ||
                    tasks->GetTransferEngine()->IsTerminal(flow.transferId))
                    continue;
                stopped = true;
                if (stopAt == "FAIL_L1")
                    tasks->GetTransferEngine()->FinalizeTransferIfActive(
                        flow.transferId,
                        TransferTerminalState::FAILED,
                        TransferTerminalReason::TASK_FAILED);
                else
                    manager.OnTaskTerminal(flow.key.taskId);
                return;
            }
        }
        if (Simulator::Now().GetNanoSeconds() + 50000 < END)
            Simulator::Schedule(NanoSeconds(50000), &Driver::Poll, this);
    }
};

void
CheckZero(const CheckpointManager& manager)
{
    for (const auto& [node, pool] : manager.Pools())
    {
        Check(pool->Used() == 0 && pool->Reserved() == 0, "terminal storage leak");
        Check(pool->PeakTotal() <= pool->Capacity(), "storage overcommit");
    }
    for (const auto& event : manager.Events())
        Check(event.remoteWork <= event.localWork && event.localWork <= event.actualWork,
              "invalid received prefix");
}

void
Lifecycle(const std::string& stopAt,
          uint64_t capacity = 10000000000ULL,
          bool delayed = false,
          bool sameNs = false,
          bool earlyEnd = false)
{
    auto config = Configuration();
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = Tasks(topology, config.parameters);
        CheckpointManager manager(tasks, topology, capacity, END);
        Driver driver(manager, tasks);
        driver.stopAt = stopAt;
        driver.delayed = delayed;
        driver.sameNs = sameNs;
        tasks->ConnectTaskObserver(MakeCallback(&Driver::Task, &driver));
        if (!stopAt.empty())
            Simulator::Schedule(NanoSeconds(1), &Driver::Poll, &driver);
        Simulator::Stop(NanoSeconds(earlyEnd ? 60000000 : END));
        Simulator::Run();
        if (earlyEnd)
            Check(std::any_of(manager.Flows().begin(),
                              manager.Flows().end(),
                              [&](const auto& flow) {
                                  return !tasks->GetTransferEngine()->IsTerminal(flow.transferId);
                              }),
                  "simulation-end fixture missed an in-flight backup");
        manager.Finalize();
        manager.Finalize();
        CheckZero(manager);
        if (!earlyEnd)
            Check(tasks->IsComplete(), "backup affected normal no-fault task completion");
        if (!stopAt.empty())
            Check(driver.stopped, "controlled cancellation phase not reached");
        const auto summaries = manager.Summaries();
        Check(summaries.size() == 1, "missing checkpoint attempt");
        const auto& summary = summaries.front();
        Check(summary.stopNs >= 0, "protection not stopped");
        if (delayed)
        {
            const auto flow =
                std::find_if(manager.Flows().begin(), manager.Flows().end(), [](const auto& f) {
                    return f.key.kind == ProtectionTransferKind::INIT_STATE;
                });
            Check(flow != manager.Flows().end(),
                  "nonzero captured initialization missing real UDP");
            TaskStateAdapter layout(tasks->GetTaskRuntimes().front().definition);
            Check(flow->bytes == layout.StateBytes(flow->work), "captured LLM init byte mismatch");
            Check(summary.initializationNs > summary.startNs, "nonzero init did not commit");
        }
        if (sameNs)
        {
            Check(summary.initializationNs == -1, "compute-end callback revived initialization");
            Check(std::none_of(manager.Events().begin(),
                               manager.Events().end(),
                               [](const auto& e) { return e.event == "INIT_STATE_GENERATED"; }),
                  "cL generation survived inclusive same-ns compute completion");
        }
        if (capacity == 1)
            Check(manager.Flows().empty(), "failed reserve sent packets");
        if (capacity == 1000000)
        {
            Check(summary.localCommits == 0, "unreserved large L1 became valid");
            Check(std::none_of(
                      manager.Flows().begin(),
                      manager.Flows().end(),
                      [](const auto& f) { return f.key.kind == ProtectionTransferKind::L1; }),
                  "unreserved L1 sent packets");
        }
        if (capacity == 30000000)
        {
            Check(summary.localCommits >= 8 && summary.remoteWork > 0 &&
                      summary.remoteWork < summary.localWork,
                  "remote capacity rejection moved r or discarded received local prefix");
            Check(std::count_if(manager.Flows().begin(),
                                manager.Flows().end(),
                                [](const auto& f) {
                                    return f.key.kind == ProtectionTransferKind::REMOTE_BATCH;
                                }) == 1,
                  "unreserved second batch sent packets");
        }
        if (stopAt == "FAIL_L1")
            Check(summary.localWork == 0 && summary.localCommits > 0,
                  "failed first L1 gap was skipped by later real receipts");
        for (const auto& flow : manager.Flows())
            Check(tasks->GetTransferEngine()->IsTerminal(flow.transferId), "live transfer leaked");
        const auto network = tasks->GetTransferEngine()->CollectCapacityAwareSummary();
        Check(network.activePathCountAtEnd == 0 && network.totalReservedRateBpsAtEnd == 0,
              "capacity admission leaked after protection cleanup");
        tasks->DisconnectTaskObserver(MakeCallback(&Driver::Task, &driver));
    }
    Reset();
}

/** Receive a later small record before an earlier large record using the real UDP engine. */
class ReceiverProbe
{
  public:
    Ptr<NetworkTransferEngine> engine;
    TaskStateAdapter layout;
    CheckpointProgress progress;
    BackupStoragePool storage{100000000};
    std::map<uint64_t, uint64_t> work, objects;
    std::vector<uint64_t> order;

    explicit ReceiverProbe(Ptr<NetworkTransferEngine> network)
        : engine(network),
          layout(TaskDefinition{1, 0, 3, 0, 52428800, 1, 78644, 0, 1, 2, TaskProfile::DENSE_IMAGE}),
          progress(layout, 0, 0)
    {
    }

    void Received(uint64_t id, int64_t time)
    {
        Check(engine->IsCompleted(id), "probe unexpected terminal");
        Check(storage.CommitReservation(objects.at(id)), "receiver reservation missing");
        Check(progress.ReceiveLocal(work.at(id), time), "duplicate receiver validity");
        order.push_back(id);
        if (id == 3)
            Check(progress.Current().localWork == 0, "later record skipped gap");
    }
};

void
OutOfOrder()
{
    auto config = Configuration("global-first");
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto engine = CreateObject<NetworkTransferEngine>();
        engine->Configure(topology,
                          "size-aware",
                          1024,
                          config.parameters.islMtuBytes,
                          config.parameters.receiverRcvBufBytes,
                          false,
                          END);
        NetworkTransfer ordinary;
        ordinary.transferId = 1;
        ordinary.sourceSatelliteId = 0;
        ordinary.destinationSatelliteId = 3;
        ordinary.sizeBytes = 1;
        engine->RegisterPlans({ordinary});
        ReceiverProbe probe(engine);
        const auto first = probe.layout.Floor(70000), second = probe.layout.Floor(75000);
        Simulator::Schedule(NanoSeconds(1), [engine] { engine->StartTransferNow(1); });
        Simulator::Schedule(NanoSeconds(1000000), [&] {
            probe.progress.ReceiveInitialization(100000, 100000);
            probe.progress.CommitRemote(1000000);
            probe.progress.Capture(first, second, 1000000);
            probe.progress.Capture(second, second, 1000000);
        });
        Simulator::Schedule(NanoSeconds(1100000), [&] {
            uint64_t previous = 0;
            for (const auto& [id, work] : {std::pair{2ULL, first}, std::pair{3ULL, second}})
            {
                auto plan = ordinary;
                plan.transferId = id;
                plan.sizeBytes = probe.layout.RecordBytes(previous, work);
                probe.work[id] = work;
                probe.objects[id] =
                    *probe.storage.TryReserve(1, StorageKind::LOCAL_RECORD, plan.sizeBytes);
                engine->RegisterRuntimePlan(plan);
                engine->StartTransferNow(id, MakeCallback(&ReceiverProbe::Received, &probe));
                previous = work;
            }
        });
        Simulator::Schedule(NanoSeconds(20000000), [&] {
            Check(engine->GetReceivedBytes(2) > 0 && !engine->IsCompleted(2),
                  "partial real L1 fixture did not receive a partial prefix");
            Check(probe.progress.Current().localWork == 0, "partial/sender-finished advanced l");
        });
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        Check(probe.order == std::vector<uint64_t>({3, 2}),
              "real records did not arrive out of order");
        Check(probe.progress.Current().localWork == second, "contiguous receipt did not close gap");
        Check(engine->AreAllTransfersCompleted(), "runtime registration failed");
        auto summaries = engine->CollectSummaries();
        for (const auto& row : summaries)
            Check(row.completionTimeNs > row.lastSendTimeNs, "sender finish mistaken for receiver");
        probe.storage.ReleaseTask(1);
        Check(probe.storage.Used() == 0 && probe.storage.Reserved() == 0, "probe storage leak");
    }
    Reset();
}

std::vector<std::string>
Canonical(bool reverse)
{
    auto config = Configuration();
    std::vector<std::string> signature;
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = Tasks(topology, config.parameters, true);
        CheckpointManager manager(tasks, topology, 10000000000ULL, END);
        Driver driver(manager, tasks);
        // Same ns multi-task creation, reverse event insertion order; nonzero init follows cL.
        for (auto id : reverse ? std::vector<uint64_t>{2, 1} : std::vector<uint64_t>{1, 2})
            Simulator::Schedule(
                NanoSeconds(20000000), &Driver::Start, &driver, id, id == 1 ? 3 : 4);
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        manager.Finalize();
        CheckZero(manager);
        for (const auto& flow : manager.Flows())
        {
            std::ostringstream out;
            out << flow.transferId << ':' << flow.key.taskId << ':' << int(flow.key.kind) << ':'
                << flow.key.sequence << ':' << flow.requestedNs;
            signature.push_back(out.str());
        }
        Check(manager.Flows().size() > 4 && manager.Flows()[0].key.taskId == 1 &&
                  manager.Flows()[1].key.taskId == 2,
              "noncanonical same-ns task order");
    }
    Reset();
    return signature;
}

void
Ids()
{
    std::set<ProtectionTransferKey> keys{{2, 0, ProtectionTransferKind::INIT_BASE, 0},
                                         {1, 0, ProtectionTransferKind::REMOTE_BATCH, 1},
                                         {1, 0, ProtectionTransferKind::INIT_STATE, 0},
                                         {1, 0, ProtectionTransferKind::L1, 2},
                                         {1, 0, ProtectionTransferKind::INIT_BASE, 0}};
    ProtectionTransferIds ids(8);
    for (const auto& key : keys)
    {
        const auto id = ids.Next();
        Check(id >= 9 && id <= 13, "ID not above normal range");
        if (id == 9)
            Check(key.kind == ProtectionTransferKind::INIT_BASE, "kind canonical order");
        if (id == 13)
            Check(key.taskId == 2, "task canonical order");
    }
    ProtectionTransferIds exhausted(std::numeric_limits<uint64_t>::max() - 1);
    Check(exhausted.Next() == std::numeric_limits<uint64_t>::max(), "last uint64 unavailable");
    bool rejected = false;
    try
    {
        exhausted.Next();
    }
    catch (const std::overflow_error&)
    {
        rejected = true;
    }
    Check(rejected, "uint64 ID wrapped into ordinary range");
}

/** New-flow queries use production ECMP capacity admission, without reservations. */
void
AdmissionPreview()
{
    auto config = Configuration();
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = Tasks(topology, config.parameters);
        auto engine = tasks->GetTransferEngine();
        uint32_t source = 0, destination = 0, blockedHop = 0;
        for (uint32_t a = 0; a < 16 && source == destination; ++a)
            for (uint32_t b = 0; b < 16; ++b)
            {
                if (a == b)
                    continue;
                const auto routes = topology.GetEcmpRouteCandidates(a, b);
                if (a != b && routes.size() == 2)
                {
                    source = a;
                    destination = b;
                    blockedHop = topology.GetNextHopSatelliteId(a, routes.front().outputInterface);
                    break;
                }
            }
        Check(source != destination, "admission fixture needs two ECMP alternatives");
        Simulator::Schedule(NanoSeconds(10000000), [&] {
            NetworkTransfer block;
            block.transferId = 3;
            block.sourceSatelliteId = source;
            block.destinationSatelliteId = blockedHop;
            block.sizeBytes = 100000000;
            engine->RegisterRuntimePlan(block);
            engine->StartTransferNow(3);
        });
        Simulator::Schedule(NanoSeconds(11000000), [&] {
            const auto before = engine->CollectCapacityAwareSummary();
            const auto preview = engine->EstimateAdmissiblePath(source, destination);
            Check(preview.admissible && preview.reachable && preview.failureReason.empty() &&
                      preview.path.hops.front().destinationSatelliteId != blockedHop,
                  "blocked first route hid admissible alternate");
            const auto local = preview.path.hops.front().destinationSatelliteId;
            PlacementContext context{source, {{local, true, true, true, true},
                                               {destination, true, true, true, false}}};
            auto probe = [&](auto a, auto b) {
                const auto path = engine->EstimateAdmissiblePath(a, b);
                return PlacementPathAvailability{path.reachable, path.admissible, path.failureReason};
            };
            const auto pairs = BuildFeasiblePlacementPairs(context, probe);
            Check(pairs.pairs == std::vector<PlacementDecision>{{local, destination}},
                  "pair builder discarded admissible alternate ECMP path");
            std::reverse(context.candidates.begin(), context.candidates.end());
            Check(BuildFeasiblePlacementPairs(context, probe).pairs == pairs.pairs,
                  "reordered pair enumeration changed real admission");
            const auto after = engine->CollectCapacityAwareSummary();
            Check(before.activePathCountAtEnd == after.activePathCountAtEnd &&
                      before.totalReservedRateBpsAtEnd == after.totalReservedRateBpsAtEnd,
                  "read-only query changed reservations");
            NetworkTransfer next;
            next.transferId = 4;
            next.sourceSatelliteId = source;
            next.destinationSatelliteId = destination;
            next.sizeBytes = 100000000;
            engine->RegisterRuntimePlan(next);
            engine->StartTransferNow(4);
        });
        Simulator::Schedule(NanoSeconds(12000000), [&] {
            Check(engine->GetTransferState(4) == TransferRuntimeState::ACTIVE,
                  "actual transfer disagrees with alternate admission preview");
            const auto blocked = engine->EstimateAdmissiblePath(source, destination);
            Check(blocked.reachable && !blocked.admissible &&
                      blocked.failureReason == "NO_ADMISSIBLE_PATH",
                  "capacity exhaustion confused with topological disconnection");
            const auto missing = engine->EstimateAdmissiblePath(source, 99);
            Check(!missing.reachable && !missing.admissible && missing.failureReason == "NO_ROUTE",
                  "missing endpoint did not return NO_ROUTE");
            engine->FinalizeTransfersIfActive(
                {3, 4},
                TransferTerminalState::CANCELLED,
                TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
            Check(engine->EstimateAdmissiblePath(source, destination).admissible,
                  "admission did not recover after release");
        });
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
    }
    Reset();
}

/** Stopping several transfers must not briefly admit/send a pending sibling. */
void
BatchCancellation()
{
    auto config = Configuration();
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = Tasks(topology, config.parameters);
        const auto engine = tasks->GetTransferEngine();
        Simulator::Schedule(NanoSeconds(10000000), [&] {
            NetworkTransfer plan;
            plan.sourceSatelliteId = 3;
            plan.destinationSatelliteId = 2;
            plan.sizeBytes = 100000000;
            const auto before = engine->GetPlans().size();
            plan.transferId = 1;
            bool rejected = false;
            try
            {
                engine->RegisterRuntimePlan(plan);
            }
            catch (const NetworkTransferConfigError&)
            {
                rejected = true;
            }
            Check(rejected && engine->GetPlans().size() == before,
                  "duplicate runtime ID mutated plans");
            plan.transferId = 3;
            plan.sizeBytes = 0;
            rejected = false;
            try
            {
                engine->RegisterRuntimePlan(plan);
            }
            catch (const NetworkTransferConfigError&)
            {
                rejected = true;
            }
            Check(rejected && engine->GetPlans().size() == before,
                  "zero UDP registration accepted");
            plan.sizeBytes = 100000000;
            for (auto id : {3ULL, 4ULL})
            {
                plan.transferId = id;
                engine->RegisterRuntimePlan(plan);
                engine->StartTransferNow(id);
            }
        });
        Simulator::Schedule(NanoSeconds(11000000), [&] {
            Check(engine->GetTransferState(4) == TransferRuntimeState::WAITING_ADMISSION,
                  "pending sibling fixture did not block on capacity");
            Check(engine->FinalizeTransfersIfActive(
                      {3, 4},
                      TransferTerminalState::CANCELLED,
                      TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER) == 2,
                  "batch finalizer did not cancel both live flows");
            Check(engine->FinalizeTransfersIfActive(
                      {3, 4},
                      TransferTerminalState::CANCELLED,
                      TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER) == 0,
                  "batch finalizer is not idempotent");
        });
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        for (const auto& row : engine->CollectSummaries())
            if (row.transferId == 4)
                Check(row.sentApplicationBytes == 0 && row.receivedApplicationBytes == 0,
                      "stopping active sibling admitted cancelled pending transfer");
        Check(tasks->IsComplete() && engine->AreAllTransfersCompleted(false) &&
                  !engine->AreAllTransfersCompleted(),
              "runtime cancellation changed ordinary success");
    }
    Reset();
}

/** cL callback is queued by the start observer before ComputeService queues completion. */
void
InclusiveCompletionBeforeUid()
{
    auto config = Configuration();
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = CreateObject<TaskCoordinator>();
        auto definition = Llm(1);
        definition.computeWorkUnits = 100;
        ComputeProfile profile{{{0, 1000000}, {2, 1000000}, {3, 1000000}}};
        tasks->Initialize(profile,
                          TaskTrace{{definition}},
                          topology,
                          "size-aware",
                          1024,
                          config.parameters.islMtuBytes,
                          config.parameters.receiverRcvBufBytes,
                          false,
                          END);
        CheckpointManager manager(tasks, topology, 10000000000ULL, END);
        Driver driver(manager, tasks);
        tasks->ConnectTaskObserver(MakeCallback(&Driver::Task, &driver));
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        manager.Finalize();
        const auto summary = manager.Summaries().front();
        Check(summary.stopReason == "COMPUTE_ENDED" && summary.initializationNs < 0,
              "earlier-UID cL callback ignored inclusive compute completion");
        Check(std::none_of(manager.Events().begin(),
                           manager.Events().end(),
                           [](const auto& e) { return e.event == "INIT_STATE_GENERATED"; }),
              "earlier-UID same-ns cL callback generated invalid state");
        Check(tasks->IsComplete(), "same-ns guard prevented actual primary completion");
        CheckZero(manager);
        tasks->DisconnectTaskObserver(MakeCallback(&Driver::Task, &driver));
    }
    Reset();
}
} // namespace

int
main()
{
    try
    {
        Ids();
        AdmissionPreview();
        BatchCancellation();
        InclusiveCompletionBeforeUid();
        OutOfOrder();
        Lifecycle("");
        Lifecycle("L1_CAPTURED");
        Lifecycle("L1_IN_FLIGHT");
        Lifecycle("BATCH_IN_FLIGHT");
        Lifecycle("REMOTE_BATCH_RECEIVED");
        Lifecycle("FAIL_L1");
        Lifecycle("", 1);
        Lifecycle("", 1000000);
        Lifecycle("", 30000000);
        Lifecycle("", 10000000000ULL, true);
        Lifecycle("", 10000000000ULL, false, true);
        Lifecycle("", 10000000000ULL, false, false, true);
        Check(Canonical(false) == Canonical(true), "event insertion order changed runtime IDs");
        std::cout << "protection path: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "protection path: " << error.what() << '\n';
        return 1;
    }
}
