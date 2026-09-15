/* SPDX-License-Identifier: GPL-2.0-only */
#include "multitree-feature-adapter.h"
#include "../../../fault/model/compute-fault-combination.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace ns3::protection::multitree
{
std::vector<RankKnot>
MakeRanks(std::vector<uint64_t> values)
{
    if (values.empty()) throw std::invalid_argument("Multi-tree calibration is empty");
    std::sort(values.begin(), values.end());
    std::vector<RankKnot> knots;
    for (size_t first = 0; first < values.size();)
    {
        size_t end = first + 1;
        while (end < values.size() && values[end] == values[first]) ++end;
        const double rank = values.size() == 1 ? 0.5 :
            (static_cast<double>(first) + static_cast<double>(end - 1)) /
                (2.0 * static_cast<double>(values.size() - 1));
        knots.push_back({values[first], rank});
        first = end;
    }
    return knots;
}

double
Percentile(const std::vector<RankKnot>& knots, uint64_t raw)
{
    if (knots.empty()) throw std::invalid_argument("Multi-tree has no calibration knots");
    if (raw <= knots.front().raw) return knots.front().percentile;
    if (raw >= knots.back().raw) return knots.back().percentile;
    const auto right = std::lower_bound(knots.begin(), knots.end(), raw,
        [](const RankKnot& knot, uint64_t value) { return knot.raw < value; });
    if (right->raw == raw) return right->percentile;
    const auto& left = *(right - 1);
    const double fraction = static_cast<double>(raw - left.raw) /
                            static_cast<double>(right->raw - left.raw);
    return left.percentile + fraction * (right->percentile - left.percentile);
}

Calibration
ReadCalibration(const std::filesystem::path& file)
{
    std::ifstream stream(file);
    if (!stream) throw std::invalid_argument("Cannot read Multi-tree calibration: " + file.string());
    const auto json = nlohmann::json::parse(stream);
    const auto read = [&](const char* name) {
        std::vector<RankKnot> knots;
        for (const auto& row : json.at(name))
        {
            RankKnot knot{row.at("raw").get<uint64_t>(), row.at("percentile").get<double>()};
            if (!std::isfinite(knot.percentile) || knot.percentile < 0 || knot.percentile > 1 ||
                (!knots.empty() && (knot.raw <= knots.back().raw ||
                                   knot.percentile <= knots.back().percentile)))
                throw std::invalid_argument("Invalid Multi-tree calibration ordering/rank");
            knots.push_back(knot);
        }
        if (knots.empty()) throw std::invalid_argument("Empty Multi-tree calibration feature");
        return knots;
    };
    return {read("input_knots"), read("deadline_knots")};
}

std::filesystem::path DefaultCalibrationPath() { return SATCOMPUTE_MULTITREE_SCALE; }

double
EquivalentIntensity(double q, int64_t intervalNs)
{
    if (!std::isfinite(q) || q < 0 || q > 1 || intervalNs <= 0)
        throw std::invalid_argument("Invalid current probability/check interval for Multi-tree FR");
    return q == 1 ? std::numeric_limits<double>::infinity() :
                   -std::log1p(-q) / (static_cast<double>(intervalNs) / 1e9);
}

Features
MapFeatures(const TaskRuntime& task, const std::vector<uint64_t>& queuedIds,
            const std::map<uint64_t, uint64_t>& inputBytes,
            const Calibration& scale, const ComputeFailurePredictionInput& prediction)
{
    if (task.computeDeadlineBudgetNs <= 0)
        throw std::invalid_argument("Multi-tree requires an established compute budget");
    Features f;
    f.inputPercentile = Percentile(scale.input, task.definition.inputBytes);
    f.deadlinePercentile = Percentile(scale.deadline, task.computeDeadlineBudgetNs);
    f.ts = 1 + 2 * f.inputPercentile;
    f.iddl = 5 + 8 * f.deadlinePercentile;
    std::set<uint64_t> seen;
    for (const auto id : queuedIds)
    {
        if (id == task.definition.taskId) continue;
        if (!seen.insert(id).second) throw std::invalid_argument("Duplicate queued task ID");
        f.queuedIds.push_back(id);
        f.cl += 1 + 2 * Percentile(scale.input, inputBytes.at(id));
    }
    f.qF1 = prediction.f1Model ? prediction.f1State.stepFailureProbability : 0;
    f.qF2 = prediction.f2Model ? prediction.f2State.stepFailureProbability : 0;
    f.qComp = CombineComputeFaultProbabilities(f.qF1, f.qF2);
    f.checkIntervalNs = prediction.checkIntervalNs;
    f.fr = EquivalentIntensity(f.qComp, f.checkIntervalNs);
    return f;
}
} // namespace ns3::protection::multitree
