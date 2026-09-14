/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_TEST_CHECKPOINT_BOUND_WITNESS_H
#define SATCOMPUTE_TEST_CHECKPOINT_BOUND_WITNESS_H
#include "ns3/checkpoint-progress.h"
#include <iostream>
#include <stdexcept>

/** Pure callback-contract witnesses, not a simulated workload or future predictor. */
inline int
CheckpointBoundWitness()
{
    using namespace ns3;
    using namespace ns3::protection;
    const auto check = [](bool condition) {
        if (!condition) throw std::runtime_error("checkpoint bound witness failed");
    };
    const auto emit = [&check](const char* name, uint64_t observed, uint64_t claimed) {
        check(observed > claimed);
        std::cout << name << '\t' << observed << '\t' << claimed << '\n';
    };
    TaskDefinition task;
    task.taskId = 1;
    task.taskProfile = TaskProfile::LLM;
    task.inputBytes = 400;
    task.computeWorkUnits = 4000;
    TaskStateAdapter layout(task);
    const auto costs = GetProtectionCosts(layout.VariableBytes());
    CheckpointProgress progress(layout, 0, 0);
    const auto initialized = progress.ReceiveInitialization(0, costs.localNs);
    check(progress.CommitRemote(initialized) == 0);
    check(!progress.BeforeFault(initialized).initialized);
    check(progress.BeforeFault(initialized + 1).initialized);

    // No implicit remote progress at the nth local receipt; receiver plus cR is required.
    int64_t now = initialized + 1000000;
    for (uint64_t work : {400, 800})
    {
        const auto generated = progress.Capture(work, work, now);
        check(progress.ReceiveLocal(work, generated + 1));
        if (work == 400)
            emit("n_one_cannot_bound_pending_gap_wu", progress.Current().localWork -
                 progress.Current().remoteWork, 0);
        now = generated + 2;
    }
    auto snap = progress.Current();
    emit("nth_receipt_gap_wu", snap.localWork - snap.remoteWork, 400);
    const auto merge = progress.ReceiveRemote(800, now);
    const auto generated = progress.Capture(1200, 1200, now + 1);
    check(generated + 1 < merge);
    check(progress.ReceiveLocal(1200, generated + 1));
    snap = progress.Current();
    emit("local_receipt_during_remote_merge_wu", snap.localWork - snap.remoteWork, 400);
    check(progress.CommitRemote(merge) == 800);
    snap = progress.BeforeFault(merge);
    emit("same_ns_commit_not_fault_valid_wu", snap.localWork - snap.remoteWork, 400);
    check(progress.BeforeFault(merge + 1).remoteWork == 800);

    // Withheld remote receipt does not stop legal contiguous local receipts.
    CheckpointProgress backlog(layout, 0, 0);
    check(backlog.CommitRemote(backlog.ReceiveInitialization(0, costs.localNs)) == 0);
    now = initialized + 1000000;
    for (uint64_t work : {400, 800, 1200, 1600})
    {
        const auto ready = backlog.Capture(work, work, now);
        check(backlog.ReceiveLocal(work, ready + 1));
        now = ready + 2;
    }
    snap = backlog.Current();
    emit("blocked_remote_gap_wu", snap.localWork - snap.remoteWork, 400);

    TaskDefinition image;
    image.taskId = 2;
    image.taskProfile = TaskProfile::DENSE_IMAGE;
    image.inputBytes = 52428800;
    image.computeWorkUnits = (image.inputBytes * 3 + 1999) / 2000;
    TaskStateAdapter tiles(image);
    const auto next = tiles.Next(0, 0, 15);
    check(next.has_value());
    emit("application_boundary_exceeds_fraction_wu", *next, tiles.Work() * 15 / 1000);
    emit("record_includes_header_bytes", tiles.RecordBytes(0, *next), tiles.StateBytes(*next));
    return 0;
}
#endif
