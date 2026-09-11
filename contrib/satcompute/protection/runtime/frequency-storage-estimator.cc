/* SPDX-License-Identifier: GPL-2.0-only */
#include "frequency-storage-estimator.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
uint64_t Add(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
        throw std::overflow_error("frequency storage peak overflow");
    return a + b;
}
} // namespace

FrequencyStorageEstimator MakeFrequencyStorageEstimator(
    const TaskDefinition& task,
    uint64_t actual,
    std::optional<CheckpointInventory> inventory)
{
    return [layout = TaskStateAdapter(task),
            input = task.inputBytes,
            actual,
            inventory = std::move(inventory)](
               FrequencyConfiguration config) -> std::optional<FrequencyStorageDemand> {
        if (!config.deltaPermille || !config.batchN || config.batchN > 1000 / config.deltaPermille)
            throw std::invalid_argument("invalid storage candidate");
        const bool on = inventory.has_value();
        if (on && (!inventory->active || !inventory->initialized))
            return std::nullopt;
        const auto initial = layout.Floor(actual);
        uint64_t triggered = on ? inventory->triggered : initial;
        uint64_t local = 0, batch = 0, maximumBatch = 0;
        uint32_t count = 0;
        auto append = [&](uint64_t bytes) {
            batch = Add(batch, bytes);
            if (++count == config.batchN)
            {
                maximumBatch = std::max(maximumBatch, batch);
                count = 0;
                batch = 0;
            }
        };
        if (on)
            for (const auto& record : inventory->records)
            {
                // Already allocated pending/valid/in-flight bytes count in pool Free().
                if (!record.allocated)
                    local = Add(local, record.bytes);
                const auto covered = inventory->batchInFlight ? inventory->batchWork
                                                              : inventory->progress.remoteWork;
                if (record.work > covered)
                    append(record.bytes);
            }
        auto next =
            on && !inventory->paused && inventory->config.deltaPermille == config.deltaPermille
                ? inventory->nextTarget
                : layout.Next(actual, triggered, config.deltaPermille);
        uint64_t futureHeaders = 0;
        while (next && *next < layout.Work())
        {
            const auto bytes = layout.RecordBytes(triggered, *next);
            local = Add(local, bytes);
            futureHeaders = Add(futureHeaders, layout.HeaderBytes());
            append(bytes);
            triggered = *next;
            next = layout.Next(triggered, triggered, config.deltaPermille);
        }
        if (!on)
        {
            // Initialization can finish later than estimated. Bound its skipped prefix
            // and eventual last capture without predicting a future queue completion.
            local =
                Add(layout.StateBytes(layout.Work()) - layout.StateBytes(initial), futureHeaders);
            maximumBatch = std::max(maximumBatch, local);
        }
        const uint64_t base = on ? inventory->baseBytes : layout.CommittedStateBytes(initial);
        // Both image and whole-token state budgets are monotone affine in work.
        const auto maximumState = std::max(
            {base, layout.CommittedStateBytes(initial), layout.CommittedStateBytes(layout.Work())});
        const auto futurePeak = Add(maximumState, maximumBatch);
        const auto occupied = Add(base, on ? inventory->batchBytes : 0);
        uint64_t remote = futurePeak > occupied ? futurePeak - occupied : 0;
        if (!on)
        {
            const auto state = initial ? Add(layout.StateBytes(initial), layout.HeaderBytes()) : 0;
            remote = std::max(Add(input, state), futurePeak);
        }
        // Only one remote batch exists at a time. The next can form only after the
        // old in-place merge, so its already charged capacity funds that state growth.
        return FrequencyStorageDemand{local, remote};
    };
}
} // namespace ns3::protection
