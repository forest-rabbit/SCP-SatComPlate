/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "ns3/command-line.h"
#include "ns3/frequency-protection-controller.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/protection-metrics.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include <iostream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;
namespace ns3::protection
{
struct FrequencyRuntimeTestAccess
{
    static CheckpointManager& Manager(FrequencyProtectionController& c) { return c.m_manager; }
};
}
namespace
{
unsigned checks{};
constexpr int64_t END = 4000000000LL;
void Check(bool pass, const std::string& reason)
{
    ++checks;
    if (!pass) throw std::runtime_error(reason);
}
FaultDefinition MakeFault(uint64_t id, uint32_t node, bool permanent = false)
{
    FaultDefinition f;
    f.faultId = id; f.nodeId = node; f.startTimeNs = Simulator::Now().GetNanoSeconds();
    f.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
    if (!permanent) { f.durationNs = 100000000; f.failureProbability = .3; f.f1Occurred = true; }
    return f;
}
struct Driver
{
    CheckpointManager& manager;
    Ptr<TaskCoordinator> tasks;
    Ptr<FaultController> faults;
    std::string mode;
    bool started{}, acted{}, refused{}, locked{};
    uint64_t savedTransfer{}, savedObject{};
    int64_t faultAt{-1};
    std::optional<uint64_t> blocker;

    void Task(const TaskEventRecord& event)
    {
        if (event.taskId != 1 || event.toState != TASK_RUNNING) return;
        ProtectionContext context;
        context.attempt = {1, 0}; context.primaryNode = 3; context.nowNs = event.simulationTimeNs;
        manager.Execute(context, {ActionKind::START_CHECKPOINT, CheckpointConfiguration{50, 4, 2, 0}});
    }
    void Fault()
    {
        faultAt = Simulator::Now().GetNanoSeconds();
        std::vector<GeneratedFaultEvent> batch;
        if (mode == "ready-source-f3") batch.push_back({FaultEventType::START, MakeFault(2, 1, true)});
        if (mode == "same-batch-holder-f3") batch.push_back({FaultEventType::START, MakeFault(2, 0, true)});
        batch.push_back({FaultEventType::START, MakeFault(1, 3, mode == "inflight-primary-f3")});
        faults->SubmitGeneratedBatch(batch);
    }
    void Poll()
    {
        const auto now = Simulator::Now().GetNanoSeconds();
        const auto inventory = manager.Inventory(1);
        if (!started && inventory && inventory->active && inventory->initialized)
        {
            if (mode == "admission-retry" && !refused)
            {
                blocker = manager.Pool(0).Allocate(999, StorageKind::REMOTE_STATE, manager.Pool(0).Free());
                Check(blocker.has_value(), "test storage blocker");
                Check(!manager.TryStartInputPrefetch(1), "unadmitted request escaped capacity");
                Check(manager.InputStaging(1).stage == InputStage::ABSENT, "unadmitted must stay ABSENT");
                manager.Pool(0).Release(*blocker);
                refused = true;
            }
            Check(manager.TryStartInputPrefetch(1), mode + ": prefetch request failed");
            started = true;
            const auto before = manager.Inventory(1)->progress;
            Check(!manager.TryStartInputPrefetch(1), "duplicate pending prefetch admitted");
            const auto after = manager.Inventory(1)->progress;
            Check(before.localWork == after.localWork && before.remoteWork == after.remoteWork,
                  "INPUT request changed checkpoint progress");
            for (auto service : tasks->GetComputeServices())
                Check(!service->HasRecoveryReservation(), "prefault INPUT reserved compute");
            if (mode == "same-ns-local")
            {
                Simulator::Schedule(NanoSeconds(1), [this] { Fault(); });
                acted = true;
            }
        }
        const auto input = manager.InputStaging(1);
        if (started && input.registeredNs >= 0 && !savedObject)
        {
            savedTransfer = input.transferId; savedObject = input.objectId;
        }
        if (started && !acted)
        {
            const bool inFlight = input.stage == InputStage::IN_FLIGHT && input.transferId &&
                tasks->GetTransferEngine()->GetSentBytes(input.transferId) > input.bytes / 2;
            const bool ready = input.stage == InputStage::READY;
            if (mode == "failed-prefetch" && inFlight)
            {
                tasks->GetTransferEngine()->FinalizeTransferIfActive(input.transferId,
                    TransferTerminalState::FAILED, TransferTerminalReason::SOURCE_SATELLITE_FAILED);
                Check(!manager.TryStartInputPrefetch(1), "failed stream restarted automatically");
                Simulator::Schedule(MilliSeconds(10), [this] {
                    Check(!manager.TryStartInputPrefetch(1), "later epoch restarted failed stream");
                    Fault();
                });
                acted = true;
            }
            else if ((mode.starts_with("inflight") || mode == "admission-retry" || mode == "postfault-transfer-failure") && inFlight)
            {
                const auto network = tasks->GetTransferEngine();
                const auto summary = network->CollectCapacityAwareSummary();
                const auto count = network->GetPlans().size();
                const auto receive = network->GetReceivedBytes(input.transferId);
                const auto estimate = network->EstimateRemainingTransferTimeNs(input.transferId);
                Check(estimate && *estimate > 0, "missing existing-flow estimate");
                for (int i = 0; i < 10; ++i)
                    Check(network->EstimateRemainingTransferTimeNs(input.transferId) == estimate, "estimate changed state");
                Check(network->GetPlans().size() == count && network->GetReceivedBytes(input.transferId) == receive &&
                    network->CollectCapacityAwareSummary().totalReservedRateBpsAtEnd == summary.totalReservedRateBpsAtEnd,
                    "estimate registered/reserved/mutated flow");
                Fault(); acted = true;
                Check(!network->IsTerminal(input.transferId), "fault cancelled reusable INPUT");
                Check(network->GetReceivedBytes(input.transferId) == receive, "handoff reset received bytes");
                if (mode == "postfault-transfer-failure")
                    Simulator::Schedule(MicroSeconds(10), [network, transfer = input.transferId] {
                        network->FinalizeTransferIfActive(transfer, TransferTerminalState::FAILED,
                                                         TransferTerminalReason::SOURCE_SATELLITE_FAILED);
                    });
            }
            else if (ready && mode != "no-fault")
            {
                Check(!manager.TryStartInputPrefetch(1), "READY created duplicate INPUT");
                const auto entry = manager.Pool(0).Find(input.objectId);
                Check(entry && !entry->reserved && entry->bytes == input.bytes, "READY missing S storage");
                if (mode == "target-changed")
                    for (auto service : tasks->GetComputeServices())
                        if (service->GetNodeId() == 0)
                        { Check(service->ReserveRecovery(999, 1), "remote busy fixture"); locked = true; }
                Fault(); acted = true;
                if (mode == "postfault-source-f3")
                    Simulator::Schedule(MilliSeconds(1), [this] {
                        faults->SubmitGeneratedBatch({{FaultEventType::START, MakeFault(2, 1, true)}});
                    });
            }
        }
        if (now + 100000 < END) Simulator::Schedule(NanoSeconds(100000), &Driver::Poll, this);
    }
};

void Run(const std::string& mode, const std::filesystem::path& output)
{
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        config.parameters.fixedDelaySeconds = .001;
        config.parameters.islBandwidthBps = 10000000000ULL;
        config.parameters.routingMode = "global-capacity-aware-hrw";
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const bool local = mode == "local-ready" || mode == "same-ns-local";
        TaskTrace trace{{{1, local ? 0u : 1u, 3, 4, 52428800, 4, 78644, 1, 1, 2, TaskProfile::DENSE_IMAGE}}};
        auto tasks = CreateObject<TaskCoordinator>();
        tasks->Initialize(ComputeProfile{{{0,100000}, {1,100000}, {2,100000}, {3,100000}, {4,100000}}},
            trace, topology, "size-aware", 1024, config.parameters.islMtuBytes,
            config.parameters.receiverRcvBufBytes, false, END, 4);
        auto faults = CreateObject<FaultController>();
        faults->ConfigureGeneration(topology.GetIdMap().GetCanonicalSatelliteIds(), END);
        faults->BindTopology(topology); faults->BindTaskCoordinator(tasks);
        auto engine = CreateObject<FaultModelEngine>(); // No fault schedule: controlled executor injection only.
        FrequencyProtectionController controller(tasks, topology, engine, 2000000000, END,
            nullptr, RemoteBusyRecoveryPolicy::RELOCATE, InputStagingPolicy::JIT);
        auto& manager = FrequencyRuntimeTestAccess::Manager(controller);
        Driver driver{manager, tasks, faults, mode};
        tasks->ConnectTaskObserver(MakeCallback(&Driver::Task, &driver));
        Simulator::Schedule(NanoSeconds(100000), &Driver::Poll, &driver);
        Simulator::Stop(NanoSeconds(END)); Simulator::Run();
        if (driver.locked)
            for (auto service : tasks->GetComputeServices()) if (service->GetNodeId() == 0) service->CancelRecovery(999, 1);
        controller.Finalize();
        Check(driver.started, "fixture never initialized");
        Check(manager.IsQuiescent(), mode + ": leaked object/flow/reservation");
        const auto input = manager.InputStaging(1);
        Check(input.stage == InputStage::RELEASED, "INPUT lifetime not closed");
        Check(input.transferId == driver.savedTransfer && input.objectId == driver.savedObject,
              "original INPUT identity replaced");
        const auto recoveries = controller.Recovery()->Summaries();
        if (mode == "no-fault")
            Check(recoveries.empty() && !input.used && input.totalSentBytes == input.bytes, "normal completion accounting");
        else
        {
            Check(driver.acted && recoveries.size() == 1, mode + ": recovery missing");
            const auto& r = recoveries.front();
            if (mode == "postfault-transfer-failure")
                Check(r.terminalState == "FAILED" && !input.used, "failed reused INPUT allowed compute");
            else Check(r.terminalState == "COMPLETED", mode + ": " + r.reason);
            const bool replaced = mode == "target-changed" || mode == "failed-prefetch" || mode == "same-batch-holder-f3";
            Check(r.inputReused == !replaced, mode + ": INPUT reuse mismatch");
            if (!replaced)
            {
                Check(r.reusedInputTransferId == input.transferId && r.reusedInputObjectId == input.objectId,
                      "handoff changed identity");
                for (const auto& f : manager.Flows())
                    Check(f.key.kind != ProtectionTransferKind::RECOVERY_INPUT, "duplicate full recovery INPUT");
                if (r.computeStartedNs >= 0) Check(r.computeStartedNs >= r.inputReceivedNs, "compute before INPUT receiver");
            }
            if (mode.starts_with("inflight") || mode == "admission-retry")
            {
                Check(r.inputStateAtAcceptance == "IN_FLIGHT" && input.sentBeforeFaultBytes > 0 &&
                      input.totalSentBytes > input.sentBeforeFaultBytes, "no actual cross-fault continuation");
                Check(r.inputReceivedNs - r.acceptedNs < input.readyNs - input.registeredNs,
                      "partial transfer produced no waiting benefit");
            }
            if (mode == "same-ns-local") Check(r.snapshot.input.stage == InputStage::IN_FLIGHT,
                                               "same-ns completion falsely prefault READY");
            if (mode == "target-changed") Check(r.recoveryNode == 1 && r.inputMode == "LOCAL",
                                                "new source=recovery target not LocalDelivery");
        }
        if (local) Check(!input.transferId && !input.totalSentBytes, "local INPUT created network traffic");
        WriteProtectionMetrics(manager, *tasks->GetTransferEngine(), output / mode);
        controller.Recovery()->WriteMetrics(output / mode);
        controller.WriteDecisions(output / mode);
        tasks->DisconnectTaskObserver(MakeCallback(&Driver::Task, &driver));
    }
    Simulator::Destroy(); Ipv4AddressGenerator::Reset(); Mac48Address::ResetAllocationIndex();
    std::cout << "JIT runtime " << mode << " passed\n";
}

void Online(const std::filesystem::path& output, bool f3)
{
    RngSeedManager::SetSeed(1); RngSeedManager::SetRun(11);
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", END, END, 6171353);
        config.parameters.fixedDelaySeconds = .001;
        config.parameters.islBandwidthBps = 10000000000ULL;
        config.parameters.routingMode = "global-capacity-aware-hrw";
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        auto faults = CreateObject<FaultController>();
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        faults->ConfigureGeneration(ids, END); faults->BindTopology(topology);
        auto engine = CreateObject<FaultModelEngine>();
        // Reuse the existing small online frequency test's fast thermal timescale.
        // This is a unit fixture, never a change to the frozen formal parameters.
        auto parameters = GetDefaultFaultParameters();
        parameters.checkIntervalSeconds = .1;
        parameters.f1.temperature.heatingToCriticalSeconds = .8;
        parameters.f2.enabled = false;
        parameters.f3.enabled = f3;
        parameters.f3.mode = "controlled"; parameters.f3.controlledNodeId = 3;
        parameters.f3.controlledStartSeconds = .4;
        engine->Configure(parameters, ids, {0,2,3,4}, END, faults, true);
        auto tasks = CreateObject<TaskCoordinator>();
        tasks->Initialize(ComputeProfile{{{0,100000},{2,100000},{3,100000},{4,100000}}},
            TaskTrace{{{1,1,3,4,52428800,4,78644,1,1,2,TaskProfile::SPARSE_INFERENCE}}},
            topology, "size-aware", 1024, config.parameters.islMtuBytes,
            config.parameters.receiverRcvBufBytes, false, END, 4);
        faults->BindTaskCoordinator(tasks); engine->BindTaskCoordinator(tasks);
        FrequencyProtectionController controller(tasks, topology, engine, 2000000000, END,
            nullptr, RemoteBusyRecoveryPolicy::RELOCATE, InputStagingPolicy::JIT);
        Simulator::Stop(NanoSeconds(END)); Simulator::Run(); controller.Finalize(); engine->Finalize();
        Check(!controller.JitDecisions().empty(), "online generate never evaluated JIT");
        unsigned initialized = 0, survived = 0;
        for (const auto& row : controller.JitDecisions())
        {
            initialized += row.trigger == "INITIALIZATION_COMMITTED";
            survived += row.trigger == "FAULT_EPOCH_SURVIVED";
            if (row.trigger == "FAULT_EPOCH_SURVIVED")
                for (const auto& frequency : controller.Decisions())
                    if (frequency.input.risk.epochNs == row.timeNs && frequency.trigger == "FAULT_EPOCH")
                    {
                        Check(!frequency.faultHit, "current fault hit performed JIT");
                        long double survival = 1, mass = 0;
                        for (const auto& step : frequency.input.risk.futureSteps)
                            if (step.targetTimeNs > row.timeNs &&
                                step.targetTimeNs < row.timeNs + static_cast<int64_t>(
                                    std::llround(frequency.input.remainingSeconds * 1e9)))
                            {
                                mass += survival * step.combinedStepFailureProbability;
                                survival *= 1-step.combinedStepFailureProbability;
                            }
                        Check(std::abs(row.decision.probabilityOn - static_cast<double>(mass)) < 1e-12,
                              "post-sample JIT repeated current probability");
                    }
        }
        Check(initialized == 1 && survived > 0, "missing actual-init or post-batch JIT hook");
        for (const auto& r : controller.Recovery()->Summaries())
            for (const auto& d : controller.JitDecisions())
                Check(d.timeNs < r.snapshot.faultNs, "JIT ran on/after actual fault");
        WriteProtectionMetrics(controller.Manager(), *tasks->GetTransferEngine(), output);
        controller.Recovery()->WriteMetrics(output); controller.WriteDecisions(output);
        Check(controller.Manager().IsQuiescent(), "online JIT leaked resources");
    }
    Simulator::Destroy(); Ipv4AddressGenerator::Reset(); Mac48Address::ResetAllocationIndex();
    std::cout << "JIT online generate " << (f3 ? "F3" : "F1") << " passed\n";
}
}
int main(int argc, char** argv)
{
    std::string output = "/tmp/satcompute-jit-runtime", only;
    CommandLine command; command.AddValue("outputDir", "fixture evidence", output);
    command.AddValue("case", "one named fixture, empty runs all", only); command.Parse(argc, argv);
    try
    {
        unsigned cases = 0;
        for (const auto mode : {"no-fault", "ready", "ready-source-f3", "inflight", "inflight-primary-f3",
                "failed-prefetch", "admission-retry", "local-ready", "same-ns-local", "target-changed",
                "same-batch-holder-f3", "postfault-source-f3", "postfault-transfer-failure"})
            if (only.empty() || only == mode) { Run(mode, output); ++cases; }
        for (const auto mode : {"online-f1", "online-f3"})
            if (only.empty() || only == mode)
            { Online(std::filesystem::path(output) / mode, std::string(mode) == "online-f3"); ++cases; }
        Check(cases > 0, "unknown fixture");
        std::cout << "JIT runtime: " << cases << " cases / " << checks << " checks passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
