/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FREQUENCY_STORAGE_ESTIMATOR_H
#define SATCOMPUTE_FREQUENCY_STORAGE_ESTIMATOR_H
#include "../mechanism/checkpoint/checkpoint-manager.h"
#include "../policy/compfrr/frequency/compfrr-frequency-policy.h"

namespace ns3::protection
{
/** Conservative additional peaks with no credit for future queue-dependent releases.
 * Holds remaining captured bytes locally; remote admits one future batch plus state
 * growth. Existing used/reserved objects are already subtracted from pool free bytes.
 */
FrequencyStorageEstimator MakeFrequencyStorageEstimator(
    const TaskDefinition& task,
    uint64_t actual,
    std::optional<CheckpointInventory> inventory);
} // namespace ns3::protection
#endif
