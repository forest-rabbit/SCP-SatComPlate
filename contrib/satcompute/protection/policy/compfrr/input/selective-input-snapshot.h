/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_SELECTIVE_INPUT_SNAPSHOT_H
#define SATCOMPUTE_SELECTIVE_INPUT_SNAPSHOT_H
#include "input-admission-policy.h"
#include "../../placement-policy.h"

namespace ns3::protection
{
/** Causal value captured after actual-pair revalidation, before START_CHECKPOINT.
 * Immutable after capture; only retained for physically admitted STARTs. No reference
 * pair, future events, reservations or secondary checkpoint/recovery predictor.
 */
struct SelectiveInputSnapshot
{
    TaskDefinition task;
    PlacementDecision pair;
    std::string trigger;
    int64_t timeNs{}, remainingNs{}, firstSampleNs{};
    bool finishExclusive{};
    AdmissiblePathEstimate inputPath;
    std::optional<ComputeFailurePrediction> prediction;
};
} // namespace ns3::protection
#endif
