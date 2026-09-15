/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_MULTITREE_FEATURE_ADAPTER_H
#define SATCOMPUTE_MULTITREE_FEATURE_ADAPTER_H

#include "../../../task/compute-task.h"
#include "../../../fault/model/compute-failure-predictor.h"
#include <filesystem>
#include <map>
#include <vector>

namespace ns3::protection::multitree
{
/** Frozen unique raw values and tied mid-ranks; no runtime recalibration. */
struct RankKnot
{
    uint64_t raw{}; ///< Integer bytes or exact deadline ns.
    double percentile{}; ///< Tied reference mid-rank in [0,1].
};
/** Fixed calibration for the two published paper-scale features. */
struct Calibration
{
    std::vector<RankKnot> input, deadline; ///< Sorted, strictly unique raw knots.
};
/** Pure tied mid-rank construction; singleton percentile is 0.5. */
std::vector<RankKnot> MakeRanks(std::vector<uint64_t> values);
/** Linear interpolation between frozen knots; clamp outside the reference range. */
double Percentile(const std::vector<RankKnot>& knots, uint64_t raw);
/** Validated frozen artifact loader (not a run configuration layer). */
Calibration ReadCalibration(const std::filesystem::path& file);
/** Tracked baseline-private calibration location. */
std::filesystem::path DefaultCalibrationPath();
/** Published tree inputs plus causal raw quantities for the audit. */
struct Features
{
    double ts{}, iddl{}, cl{}, fr{}, qF1{}, qF2{}, qComp{}; ///< Paper-scale/current-step values.
    double inputPercentile{}, deadlinePercentile{}; ///< Fixed reference ranks.
    int64_t checkIntervalNs{}; ///< Real model check interval, not the prediction horizon.
    std::vector<uint64_t> queuedIds; ///< Ordinary waiting tasks, excluding the current primary.
};
/** Equivalent Poisson intensity; q=1 yields positive infinity, never NaN. */
double EquivalentIntensity(double q, int64_t intervalNs);
/** Causal pure adapter; accepts current model copies, never future realized faults. */
Features MapFeatures(const TaskRuntime& task, const std::vector<uint64_t>& queuedIds,
                     const std::map<uint64_t, uint64_t>& inputBytes,
                     const Calibration& scale, const ComputeFailurePredictionInput& prediction);
} // namespace ns3::protection::multitree
#endif
