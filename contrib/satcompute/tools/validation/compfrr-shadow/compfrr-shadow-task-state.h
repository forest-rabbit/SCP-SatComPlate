/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_SHADOW_TASK_STATE_H
#define SATCOMPUTE_COMPFRR_SHADOW_TASK_STATE_H
#include "compfrr-shadow-model.h"
#include "ns3/event-id.h"
#include <deque>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ns3::compfrr
{
/** Per-task virtual state only: never a network transfer or compute reservation. */
struct ShadowTaskState
{
    WorkloadLayout layout;            ///< Frozen G1 mapping.
    Costs costs;                      ///< Full-state cost tier.
    uint64_t rate{};                  ///< Actual node compute rate.
    std::string mode{"OFF"};          ///< OFF, INITIALIZING, ON, RECOVERING, DONE.
    int delta{};                      ///< Current interval in per mille.
    int n{};                          ///< Current remote batch L1 count.
    uint64_t initialWork{};           ///< Legal state captured at START.
    uint64_t localWork{};             ///< Last completed virtual L1 work boundary.
    uint64_t remoteWork{};            ///< Last completed virtual remote boundary.
    uint64_t triggeredWork{};         ///< Highest already generated L1, never reinterpreted.
    int64_t startNs{-1};              ///< First observed protection START.
    int64_t onNs{-1};                 ///< Successful initialization time.
    int64_t stopNs{-1};               ///< Real compute terminal observation.
    int64_t remoteDoneNs{};           ///< Tail of per-task serialized remote queue.
    uint64_t localCount{};            ///< Completed post-initialization L1 only.
    uint64_t remoteCount{};           ///< Completed post-initialization batches only.
    uint64_t deltaChanges{};          ///< Future-target delta changes.
    uint64_t nChanges{};              ///< Batch count changes, without resetting pending L1.
    uint64_t configChanges{};         ///< Number of joint reconfigurations.
    uint64_t peakLocalTailBytes{};    ///< Completed L1 not yet on remote.
    uint64_t peakLocalStateBytes{};   ///< Largest effective local materialized prefix.
    uint64_t peakRemoteStateBytes{};  ///< Largest effective remote materialized prefix.
    uint64_t peakPendingLocalCount{}; ///< Completed L1 not yet assigned to a batch.
    uint64_t peakRemoteInFlight{};    ///< Queued or transferring virtual batches.
    uint64_t remoteInFlight{};        ///< Current virtual queue depth.
    uint64_t batchSequence{};         ///< Stable per-task audit identity.
    std::deque<std::pair<uint64_t, uint64_t>> pending; ///< (endpoint WU, incremental bytes).
    EventId nextTarget;          ///< Future untriggered target; may be replanned.
    std::vector<EventId> events; ///< Cancelled when real computation stops.
    nlohmann::json summary;      ///< Causal observation fields, null means not applicable.

    /** Begin once; only already completed legal application state may be captured. */
    void BeginInitialization(int64_t now, uint64_t initial, int deltaPermille, int remoteEvery)
    {
        if (mode != "OFF" || startNs >= 0 || layout.Floor(initial) != initial)
            throw std::logic_error("invalid shadow initialization START");
        mode = "INITIALIZING";
        startNs = now;
        initialWork = initial;
        delta = deltaPermille;
        n = remoteEvery;
    }

    /** Commit only if real computation has not stopped; initialization is not a post-ON L1. */
    bool CompleteInitialization(int64_t now)
    {
        if (mode != "INITIALIZING" || stopNs >= 0)
            return false;
        mode = "ON";
        onNs = now;
        localWork = remoteWork = triggeredWork = initialWork;
        peakLocalStateBytes = peakRemoteStateBytes = layout.StateAt(initialWork);
        return true;
    }

    /** Stop without erasing history; failed initialization incurs no invented partial cost. */
    void StopComputation(int64_t now, bool failed, bool fault)
    {
        if (stopNs >= 0)
            return;
        stopNs = now;
        if (mode == "INITIALIZING")
        {
            summary[!failed ? "initialization_completion_abort"
                    : fault ? "initialization_fault_miss"
                            : "initialization_other_abort"] = true;
        }
        mode = failed ? "RECOVERING" : "DONE";
    }

    /** @return v4 equation 17, charging initialization once only after ON. */
    double NormalCost() const
    {
        return (onNs >= 0 ? costs.local + costs.remote : 0.0) + localCount * costs.local +
               remoteCount * costs.remote;
    }
};
} // namespace ns3::compfrr
#endif
