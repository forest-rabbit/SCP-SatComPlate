/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_INPUT_START_AUDIT_H
#define SATCOMPUTE_INPUT_START_AUDIT_H
#include "../frequency/compfrr-frequency-policy.h"
#include "../../placement-policy.h"
#include "../../../../traffic/network-transfer-engine.h"

namespace ns3::protection
{
/** Already computed actual-pair revalidation inputs; no retained estimator callback. */
struct InputStartValidatedResources
{
    FrequencyInput input;
    FrequencyStorageDemand storage;
};

/** Immutable pre-START_CHECKPOINT diagnostic copy. Never used by a policy decision. */
struct InputStartAuditRecord
{
    TaskDefinition task;
    PlacementDecision pair;
    FrequencyConfiguration config;
    std::string trigger;
    int64_t timeNs{}, remainingNs{}, deadlineNs{}, firstSampleNs{}, intervalNs{};
    bool finishExclusive{}, sourceAvailable{}, remoteAvailable{};
    uint64_t progressWork{}, primaryRate{}, recoveryRate{}, localFreeBytes{}, remoteFreeBytes{};
    uint64_t headerBytes{}, legalWork{}, initialVariableStateBytes{};
    AdmissiblePathEstimate inputPath;
    std::optional<ComputeFailurePrediction> prediction;
    FrequencyInput referenceInput;
    FrequencyDecision referenceProposal;
    std::optional<PlacementDecision> referencePair;
    std::optional<InputStartValidatedResources> actualValidation;
};
} // namespace ns3::protection
#endif
