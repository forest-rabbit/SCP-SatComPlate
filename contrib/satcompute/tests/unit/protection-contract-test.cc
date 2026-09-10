/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/backup-storage-pool.h"
#include "ns3/checkpoint-progress.h"
#include "ns3/command-line.h"
#include "ns3/compfrr-shadow-model.h" // Test-only oracle, never a production dependency.
#include "ns3/fixed-protection-policy.h"
#include "ns3/para.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;

namespace
{
uint64_t checks = 0;

/** Fail a named invariant even in optimized builds. */
void
Check(bool ok, const char* message)
{
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}

/** Reject programmer-invalid input without changing global simulator state. */
template <class F>
void
Reject(F operation)
{
    try
    {
        operation();
    }
    catch (const std::invalid_argument&)
    {
        ++checks;
        return;
    }
    throw std::runtime_error("invalid protection input accepted");
}

/** Construct a representative frozen task without a network fixture. */
TaskDefinition
Task(TaskProfile profile, uint64_t input = 1000000000)
{
    TaskDefinition task;
    task.taskId = 123;
    task.taskProfile = profile;
    task.inputBytes = profile == TaskProfile::LLM ? 400 : input;
    task.computeWorkUnits = profile == TaskProfile::LLM ? 500000 : (input * 3 + 1999) / 2000;
    return task;
}

/** Exercise exact capacity and atomic in-place merge without checkpoint/network code. */
void
StorageChecks()
{
    BackupStoragePool pool(100);
    const auto a = *pool.TryReserve(1, StorageKind::LOCAL_RECORD, 60);
    const auto b = *pool.Allocate(2, StorageKind::REMOTE_STATE, 40);
    Check(pool.Free() == 0 && pool.Used() == 40 && pool.Reserved() == 60, "shared capacity");
    Check(!pool.Allocate(3, StorageKind::INIT_TEMP, 1), "reject overcommit");
    Check(!pool.Release(a) && !pool.ReleaseReservation(b), "wrong release type is inert");
    Check(pool.CommitReservation(a) && !pool.CommitReservation(a), "reservation commits once");
    Check(pool.PeakUsed() == 100 && pool.PeakReserved() == 60 && pool.PeakTotal() == 100,
          "capacity peaks");
    Check(pool.TaskPeak(1) == 60 && pool.TaskPeak(2) == 40 && pool.TaskPeak(3) == 0,
          "per-task peak must not inherit shared-pool peak");
    Check(pool.ReleaseTask(1) == 1 && pool.ReleaseTask(1) == 0 && pool.Used() == 40,
          "task-scoped cleanup");
    Check(pool.Release(b) && !pool.Release(b) && pool.Free() == 100, "no underflow");
    Check(pool.AllocationFailures() == 1, "allocation failures recorded");
    Check(pool.FailedTasks() == std::set<uint64_t>{3}, "failed allocation owner missing");
    const auto state = *pool.Allocate(1, StorageKind::REMOTE_STATE, 60);
    const auto batch = *pool.TryReserve(1, StorageKind::REMOTE_BATCH, 40);
    Check(!pool.Merge(state, batch, 80), "unreceived batch cannot merge");
    Check(pool.CommitReservation(batch), "batch received");
    Check(!pool.Merge(state, batch, 101) && pool.Used() == 100, "no implicit third allocation");
    Check(!pool.Merge(state, state, 10), "state and batch cannot alias");
    Check(pool.Merge(state, batch, 80) && pool.Used() == 80 && !pool.Find(batch),
          "in-place shrink");
    const auto foreign = *pool.Allocate(2, StorageKind::REMOTE_BATCH, 20);
    Check(!pool.Merge(state, foreign, 80), "cannot consume another task's storage");
    Check(pool.ReleaseTask(1) == 1 && pool.ReleaseTask(2) == 1, "terminal zero");
    Check(pool.TaskPeak(1) == 100 && pool.TaskPeak(2) == 40, "peaks survive merge/release");
    const auto zero = *pool.Allocate(4, StorageKind::REMOTE_STATE, 0);
    Check(pool.Find(zero) && pool.Find(zero)->bytes == 0, "zero-byte committed state exists");
    Check(pool.ReleaseTask(4) == 1, "zero object cleaned");
    const auto reservation = *pool.TryReserve(5, StorageKind::INIT_TEMP, 100);
    Check(pool.ReleaseReservation(reservation) && !pool.ReleaseReservation(reservation),
          "reservation cleanup");
    BackupStoragePool huge(std::numeric_limits<uint64_t>::max());
    const auto h =
        *huge.Allocate(1, StorageKind::REMOTE_STATE, std::numeric_limits<uint64_t>::max());
    Check(!huge.TryReserve(2, StorageKind::LOCAL_RECORD, 1) && huge.Free() == 0,
          "uint64 capacity does not wrap");
    Check(huge.Release(h) && huge.Free() == std::numeric_limits<uint64_t>::max(),
          "large release exact");
    BackupStoragePool disabled(0);
    Check(!disabled.Allocate(1, StorageKind::LOCAL_RECORD, 1), "zero-capacity experiment");
    Reject([&] { pool.Allocate(0, StorageKind::LOCAL_RECORD, 1); });
}

/** Compare exact G1 layout with independent legacy oracle; new storage formula is tested
 * separately. */
void
LayoutCheck(const TaskDefinition& task)
{
    const TaskStateAdapter adapter(task);
    const auto oracle = compfrr::MakeWorkloadLayout(task);
    Check(adapter.Work() == oracle.work && adapter.VariableBytes() == oracle.variableBytes &&
              adapter.Boundaries() == oracle.boundaries,
          "frozen application layout unchanged");
    uint64_t variableSum = 0, recordSum = 0, previous = 0;
    for (auto w : adapter.Boundaries())
    {
        Check(adapter.StateBytes(w) == oracle.StateAt(w), "exact cumulative variable state");
        if (w != 0)
        {
            variableSum += adapter.StateBytes(w) - adapter.StateBytes(previous);
            recordSum += adapter.RecordBytes(previous, w);
            Check(adapter.CommittedStateBytes(w) <=
                      adapter.CommittedStateBytes(previous) + adapter.RecordBytes(previous, w),
                  "new state fits old state plus delta");
        }
        previous = w;
    }
    Check(variableSum == adapter.VariableBytes(), "variable byte conservation");
    Check(recordSum == variableSum + (adapter.Boundaries().size() - 1) * adapter.HeaderBytes(),
          "H accounted per record only");
    Check(adapter.CommittedStateBytes(adapter.Work()) == adapter.VariableBytes(),
          "final state excludes historical H");
    for (uint32_t delta : {10, 50, 100, 200})
        Check(adapter.Next(0, 0, delta) == oracle.NextProgress(0, delta, 0),
              "legal targets match oracle");
    Check(!adapter.Next(adapter.Work(), adapter.Work(), 50), "no zero checkpoint at completion");
    Reject([&] { adapter.RecordBytes(0, 0); });
    Reject([&] { adapter.CommittedStateBytes(adapter.Work() + 1); });
    Reject([&] { adapter.Next(0, 0, 0); });
}

/** Formula endpoints, reference values and representative sizing table. */
nlohmann::json
StateChecks()
{
    nlohmann::json sizes = nlohmann::json::array();
    for (auto profile : {TaskProfile::DENSE_IMAGE,
                         TaskProfile::SPARSE_INFERENCE,
                         TaskProfile::COMPRESSION,
                         TaskProfile::LLM})
    {
        const auto task = Task(profile);
        LayoutCheck(task);
        TaskStateAdapter a(task);
        const auto before = a.CommittedStateBytes(0);
        Check(before == (profile == TaskProfile::LLM ? 0 : task.inputBytes),
              "initial base storage");
        nlohmann::json row{{"profile", TaskProfileToString(profile)},
                           {"input_bytes", task.inputBytes},
                           {"work_units", a.Work()},
                           {"variable_bytes", a.VariableBytes()},
                           {"H_bytes", a.HeaderBytes()}};
        for (auto percent : {10, 50, 100})
            row[std::to_string(percent) + "_percent_bytes"] =
                a.CommittedStateBytes(a.Work() * percent / 100);
        sizes.push_back(row);
    }
    TaskStateAdapter dense(Task(TaskProfile::DENSE_IMAGE));
    TaskStateAdapter sparse(Task(TaskProfile::SPARSE_INFERENCE));
    TaskStateAdapter compression(Task(TaskProfile::COMPRESSION));
    Check(dense.CommittedStateBytes(750000) == 1000003814, "dense half-progress hand calculation");
    Check(compression.CommittedStateBytes(750000) == 771240653,
          "compression half-progress hand calculation");
    Check(sparse.CommittedStateBytes(750000) == 500934532, "sparse half-progress hand calculation");
    TaskStateAdapter llm(Task(TaskProfile::LLM));
    Check(llm.StateBytes(199) == 114688 && llm.CommittedStateBytes(250000) == 286720000,
          "whole token KV");
    LayoutCheck(Task(TaskProfile::DENSE_IMAGE, 1));
    LayoutCheck(Task(TaskProfile::SPARSE_INFERENCE, 667));
    Reject([] { TaskStateAdapter a(Task(TaskProfile::UNSPECIFIED)); });
    Check(GetProtectionCosts(100000000).localNs == 100000 &&
              GetProtectionCosts(100000001).localNs == 500000 &&
              GetProtectionCosts(500000001).remoteNs == 8000000,
          "cost thresholds");
    return sizes;
}

/** Timing/contiguity and the storage operations which G2 will bind to actual callbacks. */
void
CheckpointChecks()
{
    TaskStateAdapter layout(Task(TaskProfile::LLM));
    CheckpointProgress progress(layout, 0, 0);
    Check(!progress.Current().initialized, "zero state is not automatic initialization");
    Reject([&] { progress.ReceiveInitialization(3000000, 1000000); });
    Check(progress.ReceiveInitialization(3000000, 2000000) == 11000000,
          "parallel initialization plus cR");
    Reject([&] { progress.CommitRemote(10999999); });
    Check(progress.CommitRemote(11000000) == 0, "zero-byte initialization commits");
    Check(!progress.BeforeFault(11000000).initialized && progress.BeforeFault(11000001).initialized,
          "same-ns init not usable");
    Check(progress.Capture(100, 100, 20000000) == 22000000, "cL delays generation");
    Check(progress.Capture(200, 300, 21000000) == 23000000, "capture immutable despite new work");
    Reject([&] { progress.ReceiveLocal(100, 21999999); });
    Check(progress.ReceiveLocal(200, 24000000) && progress.Current().localWork == 0,
          "out-of-order gap blocks local prefix");
    Check(progress.ReceiveLocal(100, 25000000) && progress.Current().localWork == 200,
          "gap closed advances prefix, not to 300");
    Check(progress.BeforeFault(25000000).localWork == 0 &&
              progress.BeforeFault(25000001).localWork == 200,
          "local same-ns fault conservative");
    Check(!progress.ReceiveLocal(100, 26000000), "duplicate receive ignored");
    BackupStoragePool local(1000000), remote(1000000);
    const auto l1 = *local.Allocate(123, StorageKind::LOCAL_RECORD, layout.RecordBytes(0, 100));
    const auto l2 = *local.Allocate(123, StorageKind::LOCAL_RECORD, layout.RecordBytes(100, 200));
    const auto base = *remote.Allocate(123, StorageKind::REMOTE_STATE, 0);
    const auto batch = *remote.TryReserve(123, StorageKind::REMOTE_BATCH, 229376);
    Check(progress.ReceiveRemote(200, 30000000) == 38000000, "remote cR delay");
    Check(remote.CommitReservation(batch), "remote receiver materializes temp batch");
    Check(progress.Current().remoteWork == 0 && local.Used() == 229376,
          "receipt is not commit or cleanup");
    Reject([&] { progress.CommitRemote(37999999); });
    Check(progress.CommitRemote(38000000) == 200 &&
              remote.Merge(base, batch, layout.CommittedStateBytes(200)),
          "RemoteCommit updates state atomically");
    Check(local.Release(l1) && local.Release(l2) && local.Used() == 0,
          "only commit covers local records");
    Check(progress.BeforeFault(38000000).remoteWork == 0 &&
              progress.BeforeFault(38000001).remoteWork == 200,
          "same-ns remote state excluded");
    Check(!progress.CommitRemote(38000001), "commit only once");
    Check(remote.Used() == 229376 && remote.Reserved() == 0, "one committed remote state");
    Check(remote.ReleaseTask(123) == 1, "takeover releases backup-only memory");
    progress.Stop();
    Check(!progress.ReceiveLocal(200, 40000000) && !progress.CommitRemote(40000000),
          "stale callbacks cannot revive stopped protection");
    CheckpointProgress miss(layout, 0, 0);
    miss.ReceiveInitialization(3000000, 2000000);
    miss.Stop();
    Check(!miss.CommitRemote(11000000) && !miss.Current().initialized,
          "initializing fault fallback remains unprotected");
    CheckpointProgress future(layout, 0, 0);
    future.ReceiveInitialization(3000000, 2000000);
    future.CommitRemote(11000000);
    Reject([&] { future.Capture(200, 100, 20000000); });
    Reject([&] { future.Capture(150, 200, 20000000); });
}

/** Callback identity, recovery immunity and causal path selection. */
void
RecoveryChecks()
{
    Check(ChooseRecoveryPath(1, 2) == RecoveryPath::TAIL, "tail estimate wins");
    Check(ChooseRecoveryPath(2, 2) == RecoveryPath::REMOTE_REDO, "tie uses redo");
    Check(ChooseRecoveryPath(3, 2) == RecoveryPath::REMOTE_REDO, "redo estimate wins");
    Check(ChooseRecoveryPath(std::nullopt, 2) == RecoveryPath::REMOTE_REDO, "missing local tail");
    Check(ChooseRecoveryPath(1, std::nullopt) == RecoveryPath::RECOMPUTE,
          "no remote base -> input replay");
    Reject([] { ChooseRecoveryPath(-1, 2); });
    ExecutionAttempt attempt(1, 4, 100);
    const auto primary = attempt.Key();
    Check(!attempt.AcceptRecovery(5, false, 10) && !attempt.AcceptRecovery(4, true, 10),
          "health and distinct node required");
    Check(attempt.AcceptRecovery(5, true, 10) && attempt.DeadlineNs() == 100,
          "accepted recovery keeps deadline");
    Check(!attempt.CompleteCompute(primary, 20) && !attempt.StartRecovery(primary, 20),
          "stale primary ignored");
    Check(attempt.ImmuneToComputeFault() && !attempt.ApplyFault(false),
          "accepted recovery ignores later F1");
    Check(!attempt.StartRecovery(attempt.Key(), 9), "dispatch cannot predate acceptance");
    Check(attempt.StartRecovery(attempt.Key(), 20) && !attempt.ApplyFault(false),
          "running recovery ignores later F2");
    Check(attempt.CompleteCompute(attempt.Key(), 100) && !attempt.ImmuneToComputeFault(),
          "inclusive deadline and end of immunity");
    Check(!attempt.ApplyFault(false), "ordinary RESULT unaffected by compute-only outage");
    Check(attempt.CompleteResult(attempt.Key()) && !attempt.CompleteResult(attempt.Key()),
          "one final result");
    ExecutionAttempt f3(2, 4, 100);
    f3.AcceptRecovery(5, true, 10);
    Check(f3.ApplyFault(true) && !f3.StartRecovery(f3.Key(),20), "F3 terminates accepted recovery");
    ExecutionAttempt runningF3(3, 4, 100);
    runningF3.AcceptRecovery(5, true, 10);
    runningF3.StartRecovery(runningF3.Key(),20);
    Check(runningF3.ApplyFault(true) && !runningF3.CompleteCompute(runningF3.Key(), 50),
          "F3 terminates running recovery");
    ExecutionAttempt resultF3(4, 4, 100);
    resultF3.AcceptRecovery(5, true, 10);
    resultF3.StartRecovery(resultF3.Key(),20);
    resultF3.CompleteCompute(resultF3.Key(), 50);
    Check(resultF3.ApplyFault(true) && !resultF3.CompleteResult(resultF3.Key()),
          "F3 still fails RESULT");
    ExecutionAttempt late(5, 4, 100);
    late.AcceptRecovery(5, true, 10);
    late.StartRecovery(late.Key(),20);
    Check(!late.CompleteCompute(late.Key(), 101) && late.Stage() == AttemptStage::FAILED,
          "late recovery fails original deadline");
    ExecutionAttempt ordinary(6, 4, 100);
    Check(ordinary.ApplyFault(false), "ordinary tasks never immune");
}

/** Test-only mechanism proves dispatcher isolation without simulating a second network. */
struct TestMechanism : ProtectionMechanism
{
    ActionKind supported;   ///< Single supported action.
    uint64_t executions{};  ///< Recorded commands.
    uint64_t completions{}; ///< Maintenance stops.
    uint64_t cleanups{};    ///< Terminal notices.
    bool acceptRecovery{};  ///< Controlled causal acceptance.

    explicit TestMechanism(ActionKind action) : supported(action)
    {
    }

    bool Supports(ActionKind kind) const override
    {
        return kind == supported;
    }

    void Execute(const ProtectionContext&, const ProtectionAction&) override
    {
        ++executions;
    }

    bool OnComputeFault(const ProtectionContext&) override
    {
        return acceptRecovery;
    }

    void OnTaskComputeComplete(AttemptKey) override
    {
        ++completions;
    }

    void OnTaskTerminal(uint64_t) override
    {
        ++cleanups;
    }
};

/** Fixed rule is stable under candidate order and dispatches each action once. */
void
PolicyChecks()
{
    const auto config = GetDefaultSatComputeConfig();
    Check(config.protectionMode == "off" && config.backupStorageBytesPerNode == 10000000000 &&
              config.fixedProtectionDelta == .05 && config.fixedProtectionBatchN == 4,
          "typed defaults");
    FixedProtectionPolicy policy(50, 4);
    TestMechanism checkpoint(ActionKind::START_CHECKPOINT), recompute(ActionKind::RECOMPUTE);
    ProtectionRuntime runtime(policy, {&checkpoint, &recompute});
    ProtectionContext context{{1, 0},
                              4,
                              0,
                              ProtectionPhase::OFF,
                              true,
                              true,
                              {{9, true, true, true, true},
                               {2, true, false, true, true},
                               {1, true, true, true, false},
                               {3, false, true, true, true},
                               {5, true, true, true, true}}};
    FixedProtectionPolicy other(50, 4);
    auto choice = other.OnTaskComputeStart(context);
    Check(choice.checkpoint && choice.checkpoint->localNode == 5 &&
              choice.checkpoint->remoteNode == 1,
          "stable lowest feasible distinct nodes");
    std::reverse(context.candidates.begin(), context.candidates.end());
    FixedProtectionPolicy reversed(50, 4);
    const auto same = reversed.OnTaskComputeStart(context);
    Check(same.checkpoint && same.checkpoint->localNode == 5 && same.checkpoint->remoteNode == 1,
          "candidate ordering irrelevant");
    runtime.OnTaskComputeStart(context);
    runtime.OnTaskComputeStart(context);
    runtime.OnProtectionEpoch(context);
    Check(checkpoint.executions == 1 && recompute.executions == 0,
          "one START and no dynamic frequency");
    runtime.OnComputeFault(context);
    Check(recompute.executions == 1, "unhandled fault goes to recompute mechanism");
    checkpoint.acceptRecovery = true;
    runtime.OnComputeFault(context);
    Check(recompute.executions == 1, "accepted checkpoint recovery suppresses fallback");
    runtime.OnTaskComputeComplete(context.attempt);
    runtime.OnTaskTerminal(1);
    Check(checkpoint.completions == 1 && recompute.cleanups == 1, "lifecycle notifications");
    context.taskSelected = false;
    Check(other.OnTaskComputeStart(context).kind == ActionKind::NONE, "unselected task stays off");
    Reject([] { FixedProtectionPolicy p(0, 4); });
    Reject([] { FixedProtectionPolicy p(50, 21); });
    Reject([&] { ProtectionRuntime r(policy, {&checkpoint, &checkpoint}); });
}
} // namespace

int
main(int argc, char** argv)
{
    std::string sizingOutput, traceInput;
    CommandLine cli;
    cli.AddValue("sizingOutput", "Optional representative state sizing JSON", sizingOutput);
    cli.AddValue(
        "traceInput", "Optional frozen task trace for full layout oracle check", traceInput);
    cli.Parse(argc, argv);
    try
    {
        StorageChecks();
        const auto sizes = StateChecks();
        CheckpointChecks();
        RecoveryChecks();
        PolicyChecks();
        if (!traceInput.empty())
        {
            std::ifstream input(traceInput);
            const auto source = nlohmann::json::parse(input);
            for (const auto& row : source.at("tasks"))
            {
                TaskDefinition task;
                task.taskId = row.at("task_id");
                task.inputBytes = row.at("input_bytes");
                task.computeWorkUnits = row.at("compute_work_units");
                const auto profile = row.at("task_profile").get<std::string>();
                task.taskProfile = profile == "dense-image"        ? TaskProfile::DENSE_IMAGE
                                   : profile == "sparse-inference" ? TaskProfile::SPARSE_INFERENCE
                                   : profile == "compression"      ? TaskProfile::COMPRESSION
                                                                   : TaskProfile::LLM;
                LayoutCheck(task);
            }
            std::cout << "Frozen task layouts checked: " << source.at("tasks").size() << '\n';
        }
        if (!sizingOutput.empty())
        {
            std::ofstream output(sizingOutput);
            output << sizes.dump(2) << '\n';
            if (!output)
                throw std::runtime_error("cannot write sizing output");
        }
        Check(Simulator::Now().GetNanoSeconds() == 0, "pure G1 contracts never advance simulation");
        std::cout << "Protection G1: " << checks << " invariant checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
