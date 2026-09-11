/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"

#include "ns3/command-line.h"
#include "ns3/frequency-protection-controller.h"
#include "ns3/least-recovery-load-placement-policy.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/protection-metrics.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;

namespace ns3::protection
{
/** Boundary injection is confined to this test executable; production RNG has no override. */
struct FrequencyRuntimeTestAccess
{
    static void Before(FrequencyProtectionController& c, const FaultEpochInput& e)
    {
        c.BeforeEpoch(e);
    }

    static void After(FrequencyProtectionController& c, int64_t time, uint64_t id, bool hit)
    {
        c.AfterEpoch(time, {{3, id, false, hit}});
    }

    static CheckpointManager& Manager(FrequencyProtectionController& c)
    {
        return c.m_manager;
    }
    static void Released(FrequencyProtectionController& c) { c.CapacityReleased(); }
    static bool HasCapacityInterest(FrequencyProtectionController& c)
    {
        return !c.m_pausedCapacity.empty() || !c.m_waitingCapacity.empty();
    }
    static bool BlockedInput(FrequencyProtectionController& c, TaskRuntime task)
    {
        task.definition.sourceNodeId = 5;
        DecisionPathSnapshot paths([](auto source, auto) {
            AdmissiblePathEstimate p;
            p.reachable = true;
            p.admissible = source != 5;
            p.failureReason = p.admissible ? "" : "NO_ADMISSIBLE_PATH";
            return p;
        });
        FrequencyDecisionRecord row;
        c.EvaluateOffPairs(row, task, c.m_states.at(task.definition.taskId), paths);
        return row.resourceReason == "NO_CAPACITY_NOW" && row.pairPathFeasible == 0 &&
               row.pairStats.skipNoCapacity > 0 && row.proposal.action == FrequencyAction::NONE;
    }
};
} // namespace ns3::protection

namespace
{
uint64_t checks{};
constexpr int64_t END = 4000000000LL;

void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
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

/** Independent exact byte checks, including already allocated and immutable pending bytes. */
void Storage()
{
    for (auto profile : {TaskProfile::DENSE_IMAGE,
                         TaskProfile::SPARSE_INFERENCE,
                         TaskProfile::COMPRESSION,
                         TaskProfile::LLM})
    {
        auto task = Definition(profile);
        TaskStateAdapter layout(task);
        const auto initial = layout.Floor(layout.Work() / 4);
        auto off = MakeFrequencyStorageEstimator(task, initial, std::nullopt)({50, 4});
        auto deferred = MakeFrequencyStorageEstimator(task, initial, std::nullopt,
                                                       InputStagingPolicy::DEFERRED)({50, 4});
        Check(deferred && deferred->remoteAdditionalBytes >= layout.StateBytes(initial) + layout.HeaderBytes(),
              "deferred initialization/merge state omitted");
        Check(deferred->remoteAdditionalBytes <= off->remoteAdditionalBytes &&
                  deferred->localAdditionalBytes == off->localAdditionalBytes,
              "deferred storage changed L1 or increased remote requirement");
        Check(off.has_value(), "OFF storage missing");
        Check(off->remoteAdditionalBytes >=
                  task.inputBytes + layout.StateBytes(initial) + layout.HeaderBytes(),
              "initialization temporary merge omitted");
        Check(off->remoteAdditionalBytes >= layout.CommittedStateBytes(initial),
              "init state omitted");
        CheckpointInventory inventory;
        inventory.active = inventory.initialized = true;
        inventory.config = {50, 4, 2, 0};
        inventory.progress = {true, initial, initial};
        inventory.actual = inventory.triggered = initial;
        inventory.baseBytes = layout.CommittedStateBytes(initial);
        inventory.nextTarget = layout.Next(initial, initial, 50);
        auto empty = MakeFrequencyStorageEstimator(task, initial, inventory)({50, 4});
        const auto work = *inventory.nextTarget;
        const auto bytes = layout.RecordBytes(initial, work);
        inventory.records.push_back({initial, work, bytes, false, false});
        inventory.triggered = work;
        inventory.nextTarget = layout.Next(work, work, 50);
        auto captured = MakeFrequencyStorageEstimator(task, work, inventory)({50, 4});
        inventory.records.front().allocated = true;
        auto allocated = MakeFrequencyStorageEstimator(task, work, inventory)({50, 4});
        Check(captured->localAdditionalBytes == allocated->localAdditionalBytes + bytes,
              "already occupied L1 charged twice or unallocated record omitted");
        inventory.batchInFlight = true;
        inventory.batchWork = work;
        inventory.batchBytes = bytes;
        auto immutable = MakeFrequencyStorageEstimator(task, work, inventory)({50, 4});
        inventory.batchBytes += 1234;
        auto alreadyCharged = MakeFrequencyStorageEstimator(task, work, inventory)({50, 4});
        Check(alreadyCharged->remoteAdditionalBytes ==
                  (immutable->remoteAdditionalBytes > 1234 ? immutable->remoteAdditionalBytes - 1234
                                                           : 0),
              "existing immutable batch capacity not deducted from additional peak");
        Check(empty->localAdditionalBytes > 0, "future capture omitted");
    }
}

/** Controlled risk epochs and actual fault executor, real network/compute/storage throughout. */
struct Driver
{
    FrequencyProtectionController& controller;
    Ptr<TaskCoordinator> tasks;
    Ptr<FaultController> executor;
    F1SelfStateFaultModel model{GetDefaultFaultParameters().f1};
    std::string mode;
    bool sawBatch{}, paused{}, resumed{}, configured{};
    std::optional<CheckpointInventory> pausedInventory;
    uint64_t immutableBytes{}, immutableWork{};
    int64_t pauseTime{};
    std::optional<uint64_t> localBlock, remoteBlock;

    void Epoch(double q, bool hit = false)
    {
        // Locate by stable ID, not profile vector order.
        Ptr<ComputeService> primary;
        for (auto service : tasks->GetComputeServices())
            if (service->GetNodeId() == 3)
                primary = service;
        auto live = primary->GetRunningTaskSnapshot();
        Check(live && live->remainingTimeNs > 0, "controlled task not running");
        const auto now = Simulator::Now().GetNanoSeconds();
        ComputeFailurePredictionInput input;
        input.f1Model = &model;
        input.f1State = model.CreateInitialSnapshot();
        // A hot synthetic trajectory has post-initialization risk. A lone overridden
        // current q on a cold state correctly has no protectable START-window mass.
        if (q > 0)
            input.f1State.temperatureC = GetDefaultFaultParameters().f1.temperature.criticalC;
        input.f1State.stepFailureProbability = q;
        input.predictionTimeNs = now;
        input.checkIntervalNs = 100000000;
        input.remainingComputeTimeNs = live->remainingTimeNs;
        FrequencyRuntimeTestAccess::Before(controller, {3, 1, q, input});
        const auto proposal = controller.Decisions().back();
        Check(proposal.input.risk.qCurrentSample == q, "current q changed");
        Check(proposal.input.risk.pFailBeforeFinish ==
                  PredictComputeFailureBeforeFinish(input).predictedFailureProbability,
              "canonical inclusive predictor wiring mismatch");
        if (hit)
        {
            FaultDefinition fault;
            fault.faultId = 1;
            fault.nodeId = 3;
            fault.faultType = FaultType::COMPUTE;
            fault.startTimeNs = now;
            fault.durationNs = 100000000;
            fault.failureProbability = q;
            fault.f1Occurred = true;
            executor->SubmitGeneratedBatch({{FaultEventType::START, fault}});
        }
        FrequencyRuntimeTestAccess::After(controller, now, 1, hit);
        const auto& row = controller.Decisions().back();
        if (hit)
        {
            Check(!row.committed && row.faultHit, "same-epoch hit committed proposal");
            Check(row.previous == row.committedConfig, "hit changed effective config");
        }
    }

    void Poll()
    {
        auto& manager = FrequencyRuntimeTestAccess::Manager(controller);
        auto inventory = manager.Inventory(1);
        if (inventory && inventory->active && inventory->initialized)
        {
            if (mode == "gate-pause" && !configured)
            {
                auto& local = manager.Pool(inventory->config.localNode);
                auto& remote = manager.Pool(inventory->config.remoteNode);
                localBlock = local.TryReserve(999, StorageKind::LOCAL_RECORD, local.Free());
                remoteBlock = remote.TryReserve(999, StorageKind::REMOTE_BATCH, remote.Free());
                Check(localBlock && remoteBlock, "controlled pool contention not established");
                Epoch(0.4);
                const auto& row = controller.Decisions().back();
                Check(row.proposal.action == FrequencyAction::PAUSE && row.committed &&
                          row.phaseAfter == ProtectionPhase::ON && manager.Inventory(1)->paused,
                      "solver PAUSE did not reach mechanism while retaining ON");
                configured = paused = true;
                pausedInventory = manager.Inventory(1);
                pauseTime = Simulator::Now().GetNanoSeconds();
            }
            else if (mode == "gate-pause" && paused && !resumed &&
                     Simulator::Now().GetNanoSeconds() > pauseTime + 100000000)
            {
                Check(inventory->triggered == pausedInventory->triggered && !inventory->nextTarget,
                      "gate PAUSE created new targets");
                manager.Pool(inventory->config.localNode).ReleaseReservation(*localBlock);
                manager.Pool(inventory->config.remoteNode).ReleaseReservation(*remoteBlock);
                Epoch(0.8);
                const auto& row = controller.Decisions().back();
                Check(row.proposal.action == FrequencyAction::UPDATE && row.committed &&
                          row.phaseAfter == ProtectionPhase::ON && !manager.Inventory(1)->paused,
                      "solver resume did not reach actual mechanism");
                resumed = true;
            }
            if (mode == "dynamic" && !configured)
            {
                Check(manager.UpdateFutureConfiguration(1, 40, 20),
                      "initial dynamic cadence rejected");
                configured = true;
            }
            if (mode == "update-hit" && controller.Decisions().size() == 1)
            {
                Epoch(1.0, true);
                Check(controller.Decisions().back().proposal.action == FrequencyAction::UPDATE,
                      "same-epoch UPDATE not proposed");
                return;
            }
            if (mode == "dynamic" && !sawBatch && !inventory->batchInFlight &&
                std::count_if(inventory->records.begin(),
                              inventory->records.end(),
                              [](const auto& record) { return record.received; }) >= 3)
            {
                const auto first = inventory->records.front().bytes;
                const auto second = inventory->records.at(1).bytes;
                Check(manager.UpdateFutureConfiguration(1, 40, 2),
                      "pending records did not adopt new n");
                inventory = manager.Inventory(1);
                Check(inventory->batchInFlight && inventory->batchBytes == first + second,
                      "new n did not form exact batch from existing valid records");
            }
            if (mode == "dynamic" && !sawBatch && inventory->batchInFlight)
            {
                sawBatch = true;
                immutableWork = inventory->batchWork;
                immutableBytes = inventory->batchBytes;
                const auto before = *inventory;
                Check(manager.UpdateFutureConfiguration(1, 20, 1), "dynamic update rejected");
                const auto after = *manager.Inventory(1);
                Check(after.batchBytes == immutableBytes && after.batchWork == immutableWork,
                      "existing batch regrouped by n update");
                Check(after.triggered == before.triggered &&
                          after.records.size() == before.records.size(),
                      "delta update rewrote records");
                Check(after.nextTarget ==
                          TaskStateAdapter(tasks->GetTaskRuntimes().front().definition)
                              .Next(after.actual, after.triggered, 20),
                      "delta ignores actual/legal boundary");
                Check(manager.PauseFutureProtection(1), "pause rejected");
                pausedInventory = manager.Inventory(1);
                pauseTime = Simulator::Now().GetNanoSeconds();
                paused = true;
            }
            else if (mode == "dynamic" && paused && !resumed &&
                     Simulator::Now().GetNanoSeconds() > pauseTime + 100000000)
            {
                const auto current = *manager.Inventory(1);
                Check(current.triggered == pausedInventory->triggered && !current.nextTarget,
                      "PAUSE captured a new target");
                Check(current.initialized &&
                          current.progress.remoteWork >= pausedInventory->progress.remoteWork,
                      "PAUSE lost state or stopped old batch");
                Check(manager.UpdateFutureConfiguration(1, 30, 1), "resume rejected");
                resumed = true;
            }
        }
        if (Simulator::Now().GetNanoSeconds() + 100000 < END)
            Simulator::Schedule(NanoSeconds(100000), &Driver::Poll, this);
    }
};

std::string Controlled(TaskProfile profile,
                       const std::string& mode,
                       const std::filesystem::path& output,
                       InputStagingPolicy inputPolicy = InputStagingPolicy::EAGER)
{
    std::string signature;
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        config.parameters.fixedDelaySeconds = 0.001;
        config.parameters.islBandwidthBps = 10000000000ULL;
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto tasks = CreateObject<TaskCoordinator>();
        tasks->Initialize(ComputeProfile{{{0, 125000}, {2, 100000}, {3, 100000}, {4, 100000}}},
                          TaskTrace{{Definition(profile)}},
                          topology,
                          "size-aware",
                          1024,
                          config.parameters.islMtuBytes,
                          config.parameters.receiverRcvBufBytes,
                          false,
                          END);
        auto executor = CreateObject<FaultController>();
        executor->ConfigureGeneration(topology.GetIdMap().GetCanonicalSatelliteIds(), END);
        executor->BindTopology(topology);
        executor->BindTaskCoordinator(tasks);
        // No generation schedule here: deterministic injection tests only the boundary seam.
        auto engine = CreateObject<FaultModelEngine>();
        FrequencyProtectionController controller(tasks, topology, engine, 10000000000ULL, END,
            nullptr, RemoteBusyRecoveryPolicy::RELOCATE, inputPolicy);
        Driver driver{controller,
                      tasks,
                      executor,
                      F1SelfStateFaultModel(GetDefaultFaultParameters().f1),
                      mode};
        Simulator::Schedule(NanoSeconds(100000000), [&] {
            Check(controller.Decisions().empty() && controller.Manager().Summaries().empty(),
                  "unconfigured synthetic engine invented a production task-start prediction");
        });
        Simulator::Schedule(NanoSeconds(200000000), [&] {
            if (inputPolicy == InputStagingPolicy::DEFERRED)
                Check(FrequencyRuntimeTestAccess::BlockedInput(controller, tasks->GetTaskRuntimes().front()),
                      "blocked deferred INPUT was not a hard pair filter/capacity retry reason");
            driver.Epoch(mode == "none" ? 0.0 : 0.4, mode == "start-hit");
            const auto& row = controller.Decisions().back();
            Check(row.pair && row.pair->localNode == 2 && row.pair->remoteNode == 0, "FFP changed");
            Check(row.input.recoveryRate == 125000, "recovery rate not actual remote profile");
            Check(row.input.localFreeBytes == 10000000000ULL &&
                      row.input.remoteFreeBytes == 10000000000ULL,
                  "pool free snapshot not live");
            Check(row.input.backupBandwidth > 0 && (inputPolicy == InputStagingPolicy::DEFERRED
                      ? row.input.baseTransferSeconds == 0 : row.input.baseTransferSeconds > 0),
                  "path estimate missing");
            if (mode == "none")
                Check(row.proposal.action == FrequencyAction::NONE &&
                          controller.Manager().Summaries().empty(),
                      "OFF NONE allocated protection");
            else
            {
                Check(row.proposal.action == FrequencyAction::START,
                      "controlled START not proposed");
                Check(row.committed == (mode != "start-hit"), "START survival gate wrong");
                if (mode == "start-hit")
                    Check(controller.Manager().Summaries().empty(),
                          "same-epoch hit created initialization");
                else
                    Check(row.phaseAfter == ProtectionPhase::INITIALIZING,
                          "START skipped real init");
            }
        });
        Simulator::Schedule(NanoSeconds(200100000), &Driver::Poll, &driver);
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        controller.Finalize();
        Check(controller.Manager().IsQuiescent(), "frequency resources leaked");
        const TaskStateAdapter layout(Definition(profile));
        if (inputPolicy == InputStagingPolicy::DEFERRED)
        {
            Check(std::none_of(controller.Manager().Flows().begin(), controller.Manager().Flows().end(),
                              [](const auto& f) { return f.key.kind == ProtectionTransferKind::INIT_BASE; }),
                  "deferred nonzero-progress initialization copied INPUT");
            for (const auto& flow : controller.Manager().Flows())
                if (flow.key.kind == ProtectionTransferKind::INIT_STATE)
                    Check(flow.work > 0 && flow.bytes == layout.StateBytes(flow.work) + layout.HeaderBytes(),
                          "deferred current state/H initialization mismatch");
        }
        uint64_t previousWork = 0;
        for (const auto& event : controller.Manager().Events())
        {
            Check(event.remoteWork <= event.localWork && event.localWork <= event.actualWork,
                  "r/l/x ordering violated");
            if (event.event == "START")
                previousWork = event.work;
            if (event.event == "L1_CAPTURED")
            {
                Check(layout.Floor(event.work) == event.work, "illegal dynamic state boundary");
                Check(event.bytes == layout.RecordBytes(previousWork, event.work),
                      "captured increment lost actual endpoints or record H");
                previousWork = event.work;
            }
        }
        std::map<uint64_t, uint64_t> l1;
        for (const auto& flow : controller.Manager().Flows())
        {
            if (flow.key.kind == ProtectionTransferKind::L1)
                l1[flow.work] = flow.bytes;
            if (flow.key.kind == ProtectionTransferKind::REMOTE_BATCH)
            {
                uint64_t sum = 0;
                for (auto it = l1.begin(); it != l1.end() && it->first <= flow.work;)
                {
                    sum += it->second;
                    it = l1.erase(it);
                }
                Check(sum == flow.bytes, "batch not exact actual record sum");
            }
        }
        if (mode == "dynamic")
            Check(driver.sawBatch && driver.paused && driver.resumed,
                  "dynamic boundary not reached");
        if (mode == "gate-pause")
            Check(driver.paused && driver.resumed, "gate PAUSE/resume not reached");
        if (mode == "update-hit")
        {
            const auto& r = controller.Decisions().back();
            Check(r.input.phase == ProtectionPhase::ON && r.previous && r.proposal.selected,
                  "UPDATE fixture missed ON");
            Check(*r.previous != r.proposal.selected->config,
                  "UPDATE fixture did not propose different config");
            Check(!controller.Recovery()->Summaries().empty(), "actual recovery not driven");
        }
        controller.WriteDecisions(output);
        WriteProtectionMetrics(controller.Manager(), *tasks->GetTransferEngine(), output);
        controller.Recovery()->WriteMetrics(output);
        std::ifstream file(output / "frequency-decisions.csv");
        signature.assign(std::istreambuf_iterator<char>(file), {});
    }
    Reset();
    return signature;
}

void Online(const std::filesystem::path& output, const std::string& mode = "normal")
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(11);
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        auto executor = CreateObject<FaultController>();
        executor->ConfigureGeneration(ids, END);
        executor->BindTopology(topology);
        auto engine = CreateObject<FaultModelEngine>();
        auto parameters = GetDefaultFaultParameters();
        std::optional<ComputeFailurePredictionInput> beforeCheck;
        if (mode == "phase-boundary")
            Simulator::Schedule(NanoSeconds(100000000), [&] {
                const auto before = engine->GetNodeSnapshots();
                beforeCheck = engine->QueryTaskPrediction(3, 200000000);
                const auto after = engine->GetNodeSnapshots();
                Check(beforeCheck && beforeCheck->firstSampleTimeNs == 100000000,
                      "pending coincident sample excluded from task-start query");
                for (size_t i = 0; i < before.size(); ++i)
                    Check(before[i].f1SampleCount == after[i].f1SampleCount &&
                              before[i].f2SampleCount == after[i].f2SampleCount &&
                              before[i].f1State.temperatureC == after[i].f1State.temperatureC,
                          "task-start query mutated physical state or sampled RNG");
            });
        parameters.checkIntervalSeconds = 0.1;
        parameters.f1.temperature.heatingToCriticalSeconds = 0.8;
        parameters.f2.enabled = true;
        parameters.f3.enabled = mode == "f3";
        parameters.f3.mode = "controlled";
        parameters.f3.controlledNodeId = 3;
        parameters.f3.controlledStartSeconds = 0.1;
        const bool paired = mode == "ffp-two" || mode == "lrl-two";
        const std::vector<uint32_t> computeNodes = paired ? ids : std::vector<uint32_t>{0, 2, 3, 4};
        engine->Configure(parameters, ids, computeNodes, END, executor, true);
        if (mode == "phase-boundary")
            Simulator::Schedule(NanoSeconds(100000000), [&] {
                const auto afterCheck = engine->QueryTaskPrediction(3, 200000000);
                Check(beforeCheck && afterCheck && afterCheck->firstSampleTimeNs == 200000000,
                      "completed coincident sample included a second time");
            });
        engine->BindOrbitConstellation(topology.GetConstellation());
        auto tasks = CreateObject<TaskCoordinator>();
        auto definition = Definition(mode == "immediate-sparse" ? TaskProfile::SPARSE_INFERENCE
                                                                : TaskProfile::LLM);
        if (mode == "immediate-sparse")
            definition.sourceNodeId =
                1; // Nonlocal INPUT replay makes protection beneficial at x=0.
        if (mode == "short")
            definition.computeWorkUnits = TaskStateAdapter::LLM_WORK_UNITS_PER_TOKEN;
        ComputeProfile compute;
        for (auto node : computeNodes) compute.nodes.push_back({node, 100000});
        TaskTrace workload{{definition}};
        if (paired)
        {
            auto next = definition;
            next.taskId = 2;
            next.inputTransferId = 3;
            next.resultTransferId = 4;
            next.computeNodeId = 5;
            next.arrivalTimeNs = 150000000;
            workload.tasks.push_back(next);
        }
        tasks->Initialize(compute, workload,
                          topology,
                          "size-aware",
                          1024,
                          config.parameters.islMtuBytes,
                          config.parameters.receiverRcvBufBytes,
                          false,
                          END);
        executor->BindTaskCoordinator(tasks);
        engine->BindTaskCoordinator(tasks);
        FrequencyProtectionController controller(tasks, topology, engine, 10000000000ULL, END,
            mode == "lrl-two" ? std::make_unique<LeastRecoveryLoadPlacementPolicy>(1)
                               : std::unique_ptr<PlacementPolicy>{});
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        controller.Finalize();
        auto trace = engine->Finalize();
        if (mode == "short")
            Check(controller.Decisions().size() == 1 && controller.Manager().Summaries().empty() &&
                      controller.Decisions().front().input.risk.pFailBeforeFinish == 0,
                  "short task missed immediate decision or invented a future check");
        else
        {
            Check(!controller.Decisions().empty(), "online generate decision absent");
            Check(controller.Decisions().front().trigger == "TASK_RUNNING" &&
                      controller.Decisions().front().input.risk.epochNs ==
                          tasks->GetTaskRuntimes().front().computeStartTimeNs,
                  "first decision did not run immediately at primary dispatch");
        }
        bool hit = false, start = false, update = false;
        if (mode == "immediate-sparse")
            Check(controller.Decisions().front().committed &&
                      controller.Decisions().front().proposal.action == FrequencyAction::START &&
                      controller.Decisions().front().phaseAfter == ProtectionPhase::INITIALIZING,
                  "beneficial short image task waited for first fault epoch to START");
        for (const auto& row : controller.Decisions())
        {
            start = start || (row.proposal.action == FrequencyAction::START && row.committed);
            if (row.trigger == "TASK_RUNNING")
            {
                Check(!row.sampled && !row.faultHit &&
                          row.firstSampleNs == 100000000 * (row.input.risk.epochNs / 100000000 + 1),
                      "task-start decision consumed a draw or used a task-relative grid");
                if (row.committed)
                    Check(row.phaseAfter == ProtectionPhase::INITIALIZING,
                          "immediate START jumped directly to ON");
                continue;
            }
            if (!row.sampled)
            {
                Check(mode == "f3" && row.faultHit && !row.committed &&
                          (row.input.phase == ProtectionPhase::ON
                              ? row.proposal.action == FrequencyAction::UPDATE ||
                                    row.proposal.action == FrequencyAction::PAUSE
                              : row.proposal.action == FrequencyAction::START ||
                                    row.proposal.action == FrequencyAction::NONE),
                      "same-time F3 leaked into proposal or committed an action");
                Check(std::none_of(controller.Manager().Events().begin(), controller.Manager().Events().end(),
                      [&](const auto& event) { return event.event == "START" &&
                          event.timeNs >= row.input.risk.epochNs; }),
                      "F3 created same-epoch initialization (an earlier causal START is legal)");
                continue;
            }
            const auto record = std::find_if(
                engine->GetProbabilityRecords().begin(),
                engine->GetProbabilityRecords().end(),
                [&](const auto& p) {
                    return p.taskId == row.taskId && p.simulationTimeNs == row.input.risk.epochNs;
                });
            Check(record != engine->GetProbabilityRecords().end(), "sampler audit record absent");
            Check(record->combinedStepFailureProbability == row.input.risk.qCurrentSample,
                  "online q not bitwise sampler equal");
            Check(record->failureBeforeFinishProbability == row.input.risk.pFailBeforeFinish,
                  "online P_finish differs from canonical audit");
            hit = hit || row.faultHit;
            start = start || (row.proposal.action == FrequencyAction::START && row.committed);
            update = update || (row.proposal.action == FrequencyAction::UPDATE && row.committed);
        }
        if (mode == "normal")
            Check(hit && start && update && !trace.faults.empty(),
                  "online generate missed start/update/actual hit");
        if (mode == "f3")
        {
            Check(engine->GetF3ComputeRiskRecords().size() == 1, "F3 causal risk snapshot missing");
            const auto& snapshot = engine->GetF3ComputeRiskRecords().front();
            Check(snapshot.taskId == 1 && snapshot.timeNs == 100000000 && snapshot.pFinish > 0 &&
                      snapshot.pFinish >= snapshot.qCompute,
                  "F3 victim causal probability was disabled");
        }
        if (paired)
        {
            std::map<uint64_t, uint32_t> selected;
            for (const auto& row : controller.Decisions())
                if (row.proposal.action == FrequencyAction::START && row.committed)
                {
                    selected[row.taskId] = row.pair->remoteNode;
                    if (row.taskId == 2)
                        Check(row.remoteLoad.activeBackup == (mode == "ffp-two" ? 1 : 0),
                              "LRL ranking did not use live remote assignments");
                }
            Check(selected.size() == 2 && selected.at(1) == 0 &&
                  selected.at(2) == (mode == "ffp-two" ? 0 : 1),
                  "paired runtime did not separate stable-ID and least-load ranking");
        }
        Check(controller.PlacementLoads().Empty(), "live placement counter leaked");
        uint64_t accepted = 0;
        for (const auto& [node, load] : controller.PlacementLoads().Nodes()) accepted += load.totalRecovery;
        const auto summaries = controller.Recovery()->Summaries();
        Check(accepted == static_cast<uint64_t>(std::count_if(summaries.begin(), summaries.end(),
              [](const auto& r) { return r.recoveryNode.has_value(); })), "actual recovery count mismatch");
        controller.WriteDecisions(output);
        controller.PlacementLoads().WriteMetrics(output);
        WriteProtectionMetrics(controller.Manager(), *tasks->GetTransferEngine(), output);
        controller.Recovery()->WriteMetrics(output);
        Check(controller.Manager().IsQuiescent(), "online generate leaked resources");
    }
    Reset();
}
/** Real capacity reservations, sub-epoch releases and fresh online predictions. */
struct RetryDriver
{
    Ptr<TaskCoordinator> tasks;
    Ptr<FaultModelEngine> engine;
    FrequencyProtectionController& controller;
    std::vector<uint64_t> blockers;
    bool terminal{}, sawStart{};
    int64_t dispatch{};

    void OnTask(const TaskEventRecord& event)
    {
        if (event.taskId != 1 || event.toState != TASK_RUNNING) return;
        dispatch = event.simulationTimeNs;
        const auto& row = controller.Decisions().back();
        Check(row.resourceReason == "NO_CAPACITY_NOW" && row.waitingAfter &&
              row.phaseAfter == ProtectionPhase::OFF && controller.Manager().Summaries().empty(),
              "all paths blocked must wait OFF without allocating checkpoint state");
        if (terminal)
            Simulator::Schedule(NanoSeconds(2000000), [&] { tasks->FinalizeSimulation(); });
        Simulator::Schedule(NanoSeconds(3000000), [&] {
            // An unrelated release leaves every primary egress blocked.
            tasks->GetTransferEngine()->FinalizeTransfersIfActive({9000},
                TransferTerminalState::CANCELLED, TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
        });
        Simulator::Schedule(NanoSeconds(5000000), [&] {
            const auto before = engine->GetNodeSnapshots();
            tasks->GetTransferEngine()->FinalizeTransfersIfActive(blockers,
                TransferTerminalState::CANCELLED, TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER);
            FrequencyRuntimeTestAccess::Released(controller);
            FrequencyRuntimeTestAccess::Released(controller);
            Simulator::ScheduleNow([&, before] {
                const auto after = engine->GetNodeSnapshots();
                for (size_t i = 0; i < before.size(); ++i)
                    Check(before[i].f1SampleCount == after[i].f1SampleCount &&
                          before[i].f2SampleCount == after[i].f2SampleCount &&
                          before[i].f1State.temperatureC == after[i].f1State.temperatureC,
                          "capacity retry consumed samples or mutated thermal state");
                uint64_t count = 0;
                for (const auto& r : controller.Decisions())
                    if (r.trigger == "CAPACITY_RELEASE" && r.input.risk.epochNs == dispatch+5000000)
                    {
                        ++count;
                        Check(r.committed && !r.sampled && !r.faultHit && r.capacityRetrySuccess &&
                              r.phaseAfter == ProtectionPhase::INITIALIZING &&
                              r.reason == "START_AFTER_CAPACITY_RELEASE" && r.progressWork > 0 &&
                              r.firstSampleNs == 100000000 && !r.waitingAfter,
                              "release retry did not perform fresh causal START before next fault epoch");
                        const auto live = tasks->GetComputeServices();
                        std::optional<RunningComputeTaskSnapshot> running;
                        for (auto s : live) if (s->GetNodeId() == 3) running = s->GetRunningTaskSnapshot();
                        Check(running.has_value(), "retry primary lost running task");
                        const auto prediction = engine->QueryTaskPrediction(3, running->remainingTimeNs);
                        Check(prediction && PredictComputeFailureBeforeFinish(*prediction).predictedFailureProbability ==
                              r.input.risk.pFailBeforeFinish, "retry reused stale prediction");
                        sawStart = true;
                    }
                Check(count == (terminal ? 0u : 1u), "same-ns releases not coalesced or terminal retried");
            });
        });
        Simulator::Schedule(NanoSeconds(20000000), [&] {
            FrequencyRuntimeTestAccess::Released(controller);
            Simulator::ScheduleNow([&] {
                Check(std::none_of(controller.Decisions().begin(), controller.Decisions().end(), [&](const auto& r) {
                    return r.trigger == "CAPACITY_RELEASE" && r.input.risk.epochNs == dispatch+20000000;
                }), "INITIALIZING/terminal task received OFF retry");
            });
        });
    }
};

void CapacityRetry(const std::filesystem::path& output, bool terminal = false, bool lrl = false)
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(11);
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        config.parameters.routingMode = "global-capacity-aware-hrw";
        config.parameters.fixedDelaySeconds = .001;
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        auto executor = CreateObject<FaultController>();
        executor->ConfigureGeneration(ids, END);
        executor->BindTopology(topology);
        auto engine = CreateObject<FaultModelEngine>();
        auto fp = GetDefaultFaultParameters();
        fp.checkIntervalSeconds = .1;
        fp.f1.temperature.heatingToCriticalSeconds = .8;
        fp.f2.enabled = fp.f3.enabled = false;
        engine->Configure(fp, ids, {0,2,3,4}, END, executor, true);
        auto task = Definition(TaskProfile::SPARSE_INFERENCE);
        task.sourceNodeId = 1;
        task.arrivalTimeNs = 20000000;
        auto tasks = CreateObject<TaskCoordinator>();
        tasks->Initialize(ComputeProfile{{{0,100000},{2,100000},{3,100000},{4,100000}}},
            TaskTrace{{task}}, topology, "size-aware", 1024, config.parameters.islMtuBytes,
            config.parameters.receiverRcvBufBytes, false, END, 1.3);
        executor->BindTaskCoordinator(tasks);
        engine->BindTaskCoordinator(tasks);
        FrequencyProtectionController controller(tasks, topology, engine, 10000000000ULL, END,
            lrl ? std::make_unique<LeastRecoveryLoadPlacementPolicy>(1) : std::unique_ptr<PlacementPolicy>{});
        // Ranking must skip a path-feasible pair rejected by real storage, not stop at it.
        auto& pool = FrequencyRuntimeTestAccess::Manager(controller).Pool(0);
        std::optional<uint64_t> storageBlock;
        if (lrl)
        {
            storageBlock = pool.TryReserve(999, StorageKind::REMOTE_BATCH, pool.Free());
            Check(storageBlock.has_value(), "hard-rejection storage blocker missing");
        }
        RetryDriver driver{tasks, engine, controller, {}, terminal};
        tasks->ConnectTaskObserver(MakeCallback(&RetryDriver::OnTask, &driver));
        Simulator::Schedule(NanoSeconds(1000000), [&] {
            auto block = [&](uint64_t id, uint32_t source, uint32_t destination) {
                NetworkTransfer transfer;
                transfer.transferId = id;
                transfer.sourceSatelliteId = source;
                transfer.destinationSatelliteId = destination;
                transfer.sizeBytes = 1000000000;
                tasks->GetTransferEngine()->RegisterRuntimePlan(transfer);
                tasks->GetTransferEngine()->StartTransferNow(id);
            };
            for (auto node : ids)
            {
                if (node == 3) continue;
                const auto routes = topology.GetEcmpRouteCandidates(3, node);
                if (std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
                    return topology.GetNextHopSatelliteId(3, route.outputInterface) == node;
                }))
                {
                    driver.blockers.push_back(9100+node);
                    block(9100+node,3,node);
                }
            }
            block(9000,6,7);
        });
        Simulator::Stop(NanoSeconds(END));
        Simulator::Run();
        controller.Finalize();
        engine->Finalize();
        Check(driver.dispatch > 0 && driver.sawStart != terminal, "capacity scenario not exercised");
        if (lrl)
        {
            const auto start = std::find_if(controller.Decisions().begin(), controller.Decisions().end(),
                [](const auto& r) { return r.capacityRetrySuccess; });
            Check(start != controller.Decisions().end() && start->pairSkipStorage > 0 &&
                  start->pairHardChecked > 1 && start->pair->remoteNode != 0,
                  "first storage-infeasible pair prevented selecting a later feasible pair");
            pool.ReleaseReservation(*storageBlock);
        }
        if (!terminal)
        {
            auto partial = std::find_if(controller.Decisions().begin(), controller.Decisions().end(), [&](const auto& r) {
                return r.trigger == "CAPACITY_RELEASE" && r.input.risk.epochNs == driver.dispatch+3000000;
            });
            Check(partial != controller.Decisions().end() && partial->waitingAfter && !partial->committed,
                  "still blocked retry did not retain waiting interest");
            const auto summaries = controller.Manager().Summaries();
            Check(std::any_of(summaries.begin(), summaries.end(),
                [](const auto& r) { return r.initCommitted > 0; }), "retry initialization never physically committed");
        }
        controller.WriteDecisions(output);
        Check(controller.Manager().IsQuiescent(), "capacity retry leaked checkpoint resources");
        Check(controller.PlacementLoads().Empty(), "capacity retry leaked placement ownership");
        tasks->DisconnectTaskObserver(MakeCallback(&RetryDriver::OnTask, &driver));
    }
    Reset();
}
/** Own real L1 reservation crosses a decision; release must resume before the next check. */
struct OnRetryDriver
{
    Ptr<TaskCoordinator> tasks;
    Ptr<FaultModelEngine> engine;
    Ptr<FaultController> executor;
    FrequencyProtectionController& controller;
    std::string mode;
    bool armed{}, released{}, checked{};
    int64_t pauseNs{}, releaseNs{};
    uint64_t flow{}, triggered{};
    uint32_t local{}, remote{};
    std::optional<uint64_t> storageBlock;

    void Reserved(uint32_t node, uint32_t, uint64_t rate)
    {
        if (node != 3) return;
        const auto inv = controller.Manager().Inventory(1);
        if (!armed && rate && inv && inv->initialized)
        {
            armed = true;
            local = inv->config.localNode;
            remote = inv->config.remoteNode;
            Simulator::ScheduleNow(&OnRetryDriver::Pause, this);
        }
        else if (armed && !released && !rate && pauseNs)
        {
            released = true;
            releaseNs = Simulator::Now().GetNanoSeconds();
            // This callback precedes the controller's deferred capacity drain.
            Simulator::ScheduleNow([this] {
                if (mode == "fault")
                {
                    FaultDefinition fault;
                    fault.faultId = 900;
                    fault.nodeId = 3;
                    fault.faultType = FaultType::COMPUTE;
                    fault.startTimeNs = releaseNs;
                    fault.durationNs = 100000000;
                    fault.failureProbability = .4;
                    fault.f1Occurred = true;
                    executor->SubmitGeneratedBatch({{FaultEventType::START, fault}});
                }
                else if (mode == "terminal") tasks->FinalizeSimulation();
                // Fault/terminal execution legitimately advances thermal state; isolate retry.
                const auto before = engine->GetNodeSnapshots();
                FrequencyRuntimeTestAccess::Released(controller);
                FrequencyRuntimeTestAccess::Released(controller);
                Simulator::ScheduleNow([this, before] { Verify(before); });
            });
        }
    }

    void Pause()
    {
        auto& manager = FrequencyRuntimeTestAccess::Manager(controller);
        auto net = tasks->GetTransferEngine();
        for (const auto& f : manager.Flows())
            if (f.key.kind == ProtectionTransferKind::L1 &&
                net->GetTransferState(f.transferId) == TransferRuntimeState::ACTIVE)
                flow = f.transferId;
        Check(flow != 0, "ON test did not find its own active L1 flow");
        const auto path = net->EstimateAdmissiblePath(3, local);
        Check(path.reachable && !path.admissible && path.failureReason == "NO_ADMISSIBLE_PATH",
              "own L1 reservation must still block new-flow admission");
        if (mode == "storage")
        {
            auto& pool = manager.Pool(local);
            storageBlock = pool.TryReserve(999, StorageKind::LOCAL_RECORD, pool.Free());
            Check(storageBlock.has_value(), "ON storage blocker missing");
        }
        std::optional<RunningComputeTaskSnapshot> live;
        for (auto s : tasks->GetComputeServices())
            if (s->GetNodeId() == 3) live = s->GetRunningTaskSnapshot();
        Check(live.has_value(), "ON primary not running");
        const auto p = engine->QueryTaskPrediction(3, live->remainingTimeNs);
        Check(p.has_value(), "ON causal prediction missing");
        pauseNs = Simulator::Now().GetNanoSeconds();
        const auto q = CombineComputeFaultProbabilities(p->f1State.stepFailureProbability, 0);
        FrequencyRuntimeTestAccess::Before(controller, {3, 1, q, *p});
        FrequencyRuntimeTestAccess::After(controller, pauseNs, 1, false);
        const auto& row = controller.Decisions().back();
        Check(row.committed && row.proposal.action == FrequencyAction::PAUSE &&
              row.resourceReason == "NO_ADMISSIBLE_PATH" && manager.Inventory(1)->paused &&
              FrequencyRuntimeTestAccess::HasCapacityInterest(controller), "ON did not register release interest");
        triggered = manager.Inventory(1)->triggered;
        // Spurious/duplicate notifications cannot bypass the still-active reservation.
        FrequencyRuntimeTestAccess::Released(controller);
        FrequencyRuntimeTestAccess::Released(controller);
        Simulator::ScheduleNow([this] {
            const auto& r = controller.Decisions().back();
            Check(r.trigger == "CAPACITY_RELEASE" && r.input.risk.epochNs == pauseNs &&
                  r.proposal.action == FrequencyAction::PAUSE && !r.capacityRetrySuccess &&
                  controller.Manager().Inventory(1)->triggered == triggered,
                  "same-ns ON retry bypassed capacity or generated historical targets");
        });
    }

    void Verify(const std::vector<FaultModelNodeSnapshot>& before)
    {
        const auto after = engine->GetNodeSnapshots();
        for (size_t i = 0; i < before.size(); ++i)
            Check(before[i].f1SampleCount == after[i].f1SampleCount &&
                  before[i].f2SampleCount == after[i].f2SampleCount &&
                  before[i].f1State.temperatureC == after[i].f1State.temperatureC,
                  "ON retry consumed fault samples or changed thermal state");
        uint64_t count = 0;
        for (const auto& r : controller.Decisions())
            if (r.trigger == "CAPACITY_RELEASE" && r.input.risk.epochNs == releaseNs)
            {
                ++count;
                Check(r.input.phase == ProtectionPhase::ON && !r.sampled && !r.faultHit &&
                      r.pair->localNode == local && r.pair->remoteNode == remote &&
                      r.capacityWaitStartNs == -1 && r.capacityWaitEndNs == -1,
                      "ON retry changed pair or mixed OFF waiting/sample accounting");
                if (mode == "storage")
                    Check(r.proposal.action == FrequencyAction::PAUSE && !r.capacityRetrySuccess,
                          "capacity resume bypassed storage");
                else
                    Check(r.committed && r.capacityRetrySuccess &&
                          r.reason == "RESUME_AFTER_CAPACITY_RELEASE" &&
                          r.proposal.action == FrequencyAction::UPDATE && r.progressWork > 0 &&
                          r.firstSampleNs == 1000000000,
                          "ON did not resume with fresh progress/prediction at real release");
            }
        Check(count == ((mode == "fault" || mode == "terminal") ? 0u : 1u),
              "ON retry duplicated or resumed a failed/terminal task");
        Check(!FrequencyRuntimeTestAccess::HasCapacityInterest(controller),
              "completed retry retained interest after resume/stop/noncapacity pause");
        if (mode == "normal")
        {
            const auto inv = controller.Manager().Inventory(1);
            Check(inv && !inv->paused && inv->triggered == triggered && inv->nextTarget &&
                  *inv->nextTarget > inv->actual && inv->progress.localWork > 0 &&
                  releaseNs-pauseNs < 10000000 && releaseNs < 1000000000,
                  "resume lost completed L1, replayed old work or waited until next epoch");
        }
        const auto size = controller.Decisions().size();
        FrequencyRuntimeTestAccess::Released(controller);
        Simulator::ScheduleNow([this, size] {
            Check(controller.Decisions().size() == size, "unpaused/noncapacity/terminal ON retried");
            checked = true;
        });
    }
};

void OnCapacityRetry(const std::filesystem::path& output, const std::string& mode = "normal",
                     InputStagingPolicy staging = InputStagingPolicy::EAGER)
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(11);
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        config.parameters.routingMode = "global-capacity-aware-hrw";
        config.parameters.fixedDelaySeconds = .001;
        config.parameters.islBandwidthBps = 10000000000ULL;
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        auto executor = CreateObject<FaultController>();
        executor->ConfigureGeneration(ids, END);
        executor->BindTopology(topology);
        auto engine = CreateObject<FaultModelEngine>();
        auto fp = GetDefaultFaultParameters();
        fp.checkIntervalSeconds = 1;
        fp.f1.temperature.heatingToCriticalSeconds = .8;
        fp.f2.enabled = fp.f3.enabled = false;
        engine->Configure(fp, ids, {0,2,3,4}, END, executor, true);
        auto task = Definition(TaskProfile::SPARSE_INFERENCE);
        task.inputBytes = 200000000;
        task.computeWorkUnits = 300000;
        auto tasks = CreateObject<TaskCoordinator>();
        tasks->Initialize(ComputeProfile{{{0,100000},{2,100000},{3,100000},{4,100000}}},
            TaskTrace{{task}}, topology, "size-aware", 1024, config.parameters.islMtuBytes,
            config.parameters.receiverRcvBufBytes, false, END, 1.3);
        executor->BindTaskCoordinator(tasks);
        engine->BindTaskCoordinator(tasks);
        FrequencyProtectionController controller(tasks, topology, engine, 10000000000ULL, END,
            nullptr, RemoteBusyRecoveryPolicy::RELOCATE, staging);
        OnRetryDriver driver{tasks, engine, executor, controller, mode};
        tasks->GetTransferEngine()->SetCapacityReservationObserver(MakeCallback(&OnRetryDriver::Reserved, &driver));
        Simulator::Stop(NanoSeconds(900000000));
        Simulator::Run();
        Check(driver.armed && driver.released && driver.checked, "ON release scenario not fully exercised");
        tasks->GetTransferEngine()->SetCapacityReservationObserver({});
        if (driver.storageBlock)
            FrequencyRuntimeTestAccess::Manager(controller).Pool(driver.local).ReleaseReservation(*driver.storageBlock);
        controller.Finalize();
        engine->Finalize();
        controller.WriteDecisions(output);
        WriteProtectionMetrics(controller.Manager(), *tasks->GetTransferEngine(), output);
        Check(controller.Manager().IsQuiescent() && controller.PlacementLoads().Empty() &&
              !FrequencyRuntimeTestAccess::HasCapacityInterest(controller), "ON retry leaked resources/interest");
    }
    Reset();
}
} // namespace

int main(int argc, char** argv)
{
    std::string output = "/tmp/satcompute-frequency-runtime";
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Controlled evidence directory", output);
    command.Parse(argc, argv);
    try
    {
        Storage();
        for (const auto& mode : {"normal", "fault", "terminal", "storage"})
            OnCapacityRetry(std::filesystem::path(output) / (std::string("on-capacity-") + mode), mode);
        OnCapacityRetry(std::filesystem::path(output) / "on-capacity-deferred", "normal", InputStagingPolicy::DEFERRED);
        OnCapacityRetry(std::filesystem::path(output) / "on-capacity-repeat");
        for (const auto& name : {"frequency-decisions.csv", "frequency-pause-intervals.csv"})
        {
            std::ifstream a(std::filesystem::path(output) / "on-capacity-normal" / name);
            std::ifstream b(std::filesystem::path(output) / "on-capacity-repeat" / name);
            Check(a.good() && b.good() && std::string(std::istreambuf_iterator<char>(a), {}) ==
                  std::string(std::istreambuf_iterator<char>(b), {}), "ON release retry repeat differs");
        }
        CapacityRetry(std::filesystem::path(output) / "capacity-retry");
        CapacityRetry(std::filesystem::path(output) / "capacity-retry-repeat");
        CapacityRetry(std::filesystem::path(output) / "capacity-retry-lrl", false, true);
        CapacityRetry(std::filesystem::path(output) / "capacity-retry-terminal", true);
        for (const auto& name : {"frequency-decisions.csv", "frequency-capacity-waits.csv"})
        {
            std::ifstream a(std::filesystem::path(output) / "capacity-retry" / name);
            std::ifstream b(std::filesystem::path(output) / "capacity-retry-repeat" / name);
            Check(a.good() && b.good() && std::string(std::istreambuf_iterator<char>(a), {}) ==
                  std::string(std::istreambuf_iterator<char>(b), {}), "capacity retry repeat differs");
        }
        for (auto profile : {TaskProfile::DENSE_IMAGE,
                             TaskProfile::SPARSE_INFERENCE,
                             TaskProfile::COMPRESSION,
                             TaskProfile::LLM})
            Controlled(profile,
                       "dynamic",
                       std::filesystem::path(output) / TaskProfileToString(profile));
        for (const auto& mode : {"none", "start-hit", "update-hit", "gate-pause"})
            Controlled(TaskProfile::LLM, mode, std::filesystem::path(output) / mode);
        const auto first =
            Controlled(TaskProfile::LLM, "dynamic", std::filesystem::path(output) / "repeat-a");
        const auto second =
            Controlled(TaskProfile::LLM, "dynamic", std::filesystem::path(output) / "repeat-b");
        Check(first == second, "repeated controlled decisions differ");
        for (auto profile : {TaskProfile::DENSE_IMAGE, TaskProfile::SPARSE_INFERENCE,
                             TaskProfile::COMPRESSION, TaskProfile::LLM})
            Controlled(profile, "dynamic", std::filesystem::path(output) / (std::string("deferred-") + TaskProfileToString(profile)),
                       InputStagingPolicy::DEFERRED);
        Online(std::filesystem::path(output) / "online-generate");
        Online(std::filesystem::path(output) / "online-immediate-sparse", "immediate-sparse");
        Online(std::filesystem::path(output) / "online-phase-boundary", "phase-boundary");
        Online(std::filesystem::path(output) / "online-short", "short");
        Online(std::filesystem::path(output) / "online-f3", "f3");
        Online(std::filesystem::path(output) / "online-ffp-two", "ffp-two");
        Online(std::filesystem::path(output) / "online-lrl-two", "lrl-two");
        Online(std::filesystem::path(output) / "online-lrl-repeat", "lrl-two");
        for (const auto& name : {"frequency-decisions.csv", "placement-load-events.csv", "placement-node-summary.csv"})
        {
            std::ifstream a(std::filesystem::path(output) / "online-lrl-two" / name);
            std::ifstream b(std::filesystem::path(output) / "online-lrl-repeat" / name);
            Check(std::string(std::istreambuf_iterator<char>(a), {}) ==
                  std::string(std::istreambuf_iterator<char>(b), {}), "LRL repeated output differs");
        }
        std::cout << "frequency-runtime: PASS (" << checks << " checks)\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        Reset();
        return 1;
    }
}
