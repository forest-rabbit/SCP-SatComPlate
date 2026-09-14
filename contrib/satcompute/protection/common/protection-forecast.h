/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_FORECAST_H
#define SATCOMPUTE_PROTECTION_FORECAST_H
#include "task-state-adapter.h"
#include <vector>

namespace ns3::protection
{
/** One causal future sampler probability, not a sampled event or new decision variable. */
struct ProtectionRiskStep
{
    int64_t targetTimeNs{}; ///< Absolute canonical fault-check time.
    double combinedStepFailureProbability{}; ///< Conditional F1/F2 union.
};

/** Same-epoch causal probabilities. Contains no trace, sample outcome or future fault ID. */
struct ProtectionRisk
{
    int64_t epochNs{};          ///< Current fault-check time, not a task-relative timer.
    int64_t intervalNs{};       ///< Current sample's reference interval and ON cost window.
    double qCurrentSample{};    ///< Union of the unchanged independent F1/F2 samples.
    double pFailBeforeFinish{}; ///< Canonical predictor, including the current check.
    std::vector<ProtectionRiskStep> futureSteps; ///< Read-only canonical trajectory for START.
};

/** Immutable frequency only; placement is supplied separately by PlacementPolicy. */
struct CheckpointCadence
{
    uint32_t deltaPermille{}; ///< 10..100, step one (0.1 percentage point).
    uint32_t batchN{};        ///< 1..100, n*deltaPermille<=1000.
    bool operator==(const CheckpointCadence&) const = default;
};

/** Read-only resource/risk demand; no Frequency solver or storage callback. */
struct PlacementForecastInput
{
    InputStagingPolicy inputPolicy{InputStagingPolicy::EAGER};
    ProtectionRisk risk;
    double inputBytes{}, work{}, variableBytes{}, progress{}, primaryRate{}, recoveryRate{};
    double inputBandwidth{}, backupBandwidth{};
    int64_t deadlineNs{};
    ProtectionCosts costs{};
};

} // namespace ns3::protection
#endif
