/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-shadow-model.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace ns3::compfrr
{
namespace
{
uint64_t
MulDiv(uint64_t a, uint64_t b, uint64_t divisor, bool ceil = false)
{
    const unsigned __int128 product = static_cast<unsigned __int128>(a) * b;
    const auto result = (product + (ceil ? divisor - 1 : 0)) / divisor;
    if (result > std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("G1 shadow integer budget overflow");
    return static_cast<uint64_t>(result);
}

void
Validate(const DecisionInput& in)
{
    for (double value : {in.inputBytes,
                         in.work,
                         in.variableBytes,
                         in.rate,
                         in.bandwidth,
                         in.progress,
                         in.remainingSeconds,
                         in.deadlineSlack,
                         in.qOneSecond,
                         in.pFinish,
                         in.costs.local,
                         in.costs.remote})
        if (!std::isfinite(value))
            throw std::invalid_argument("non-finite shadow input");
    if (in.inputBytes < 0 || in.work <= 0 || in.variableBytes < 0 || in.rate <= 0 ||
        in.bandwidth <= 0 || in.progress < 0 || in.progress > 1 || in.remainingSeconds < 0 ||
        in.qOneSecond < 0 || in.qOneSecond > 1 || in.pFinish < 0 || in.pFinish > 1 ||
        in.costs.local < 0 || in.costs.remote < 0)
        throw std::invalid_argument("invalid shadow input range");
}
} // namespace

uint64_t
WorkloadLayout::StateAt(uint64_t completedWork) const
{
    if (completedWork > work)
        throw std::invalid_argument("shadow state exceeds task work");
    return tokens ? (completedWork / 400) * 114688 : MulDiv(variableBytes, completedWork, work);
}

uint64_t
WorkloadLayout::Floor(uint64_t completedWork) const
{
    return *std::prev(std::upper_bound(boundaries.begin(), boundaries.end(), completedWork));
}

std::optional<uint64_t>
WorkloadLayout::Next(uint64_t targetWork, uint64_t previousWork) const
{
    auto found = std::lower_bound(boundaries.begin(), boundaries.end(), targetWork);
    auto after = std::upper_bound(boundaries.begin(), boundaries.end(), previousWork);
    found = std::max(found, after);
    return found == boundaries.end() ? std::nullopt : std::optional<uint64_t>(*found);
}

std::optional<uint64_t>
WorkloadLayout::NextProgress(uint64_t baseWork, int deltaPermille, uint64_t previousWork) const
{
    const unsigned __int128 numerator = static_cast<unsigned __int128>(extent) *
                                        (static_cast<unsigned __int128>(baseWork) * 1000 +
                                         static_cast<unsigned __int128>(work) * deltaPermille);
    const unsigned __int128 denominator = static_cast<unsigned __int128>(work) * 1000;
    const auto target = (numerator + denominator - 1) / denominator;
    if (target > extent)
        return std::nullopt;
    const auto end = std::lower_bound(applicationEnds.begin(),
                                      applicationEnds.end(),
                                      static_cast<uint64_t>(target));
    if (end == applicationEnds.end())
        return std::nullopt;
    return Next(boundaries.at(end - applicationEnds.begin()), previousWork);
}

WorkloadLayout
MakeWorkloadLayout(const TaskDefinition& task)
{
    if (task.inputBytes == 0 || task.computeWorkUnits == 0)
        throw std::invalid_argument("shadow G1 requires positive input and WU");
    WorkloadLayout layout;
    layout.work = task.computeWorkUnits;
    layout.boundaries.push_back(0);
    layout.applicationEnds.push_back(0);
    if (task.taskProfile == TaskProfile::LLM)
    {
        if (layout.work % 400 || layout.work / 400 > 40960)
            throw std::invalid_argument("shadow LLM must follow 400 WU/token and context limit");
        layout.tokens = true;
        layout.extent = layout.work / 400;
        layout.variableBytes = MulDiv(layout.work, 114688, 400);
        for (uint64_t w = 400; w <= layout.work; w += 400)
        {
            layout.boundaries.push_back(w);
            layout.applicationEnds.push_back(w / 400);
        }
        return layout;
    }
    if (layout.work != MulDiv(task.inputBytes, 3, 2000, true))
        throw std::invalid_argument("shadow image WU differs from frozen G1 mapping");
    uint64_t reference = 52428800;
    uint64_t state = 0;
    switch (task.taskProfile)
    {
    case TaskProfile::DENSE_IMAGE:
        state = 52428800 + 400;
        break;
    case TaskProfile::COMPRESSION:
        state = 28440844 + 800;
        break;
    case TaskProfile::SPARSE_INFERENCE:
        reference = 26246291;
        state = 48256 + 800;
        break;
    default:
        throw std::invalid_argument("shadow requires an explicit G1 task profile");
    }
    layout.variableBytes = MulDiv(task.inputBytes, state, reference);
    layout.extent = task.inputBytes;
    const bool files = task.taskProfile == TaskProfile::SPARSE_INFERENCE;
    const uint64_t count =
        files ? std::min(task.inputBytes, MulDiv(task.inputBytes, 100, reference, true))
              : (task.inputBytes - 1) / 524288 + 1;
    const uint64_t base = task.inputBytes / count;
    const uint64_t remainder = task.inputBytes % count;
    for (uint64_t i = 1; i <= count; ++i)
    {
        const uint64_t end =
            files ? i * base + std::min(i, remainder) : std::min(i * 524288, task.inputBytes);
        const uint64_t work = MulDiv(layout.work, end, task.inputBytes, true);
        if (work != layout.boundaries.back())
        {
            layout.boundaries.push_back(work);
            layout.applicationEnds.push_back(end);
        }
        else
            layout.applicationEnds.back() = end;
    }
    return layout;
}

Costs
CostTier(uint64_t bytes)
{
    if (bytes <= 100000000)
        return {0.0001, 0.0005, 1};
    if (bytes <= 500000000)
        return {0.0005, 0.002, 2};
    return {0.002, 0.008, 3};
}

Selection
SelectFrequency(const DecisionInput& in, bool alreadyOn)
{
    Validate(in);
    Selection result;
    if (!in.nodeAvailable || !in.pathAvailable || in.deadlineSlack < 0)
        return result;
    for (int d = 10; d <= 100; ++d)
        for (int n = 1; n <= 100 && n * d <= 1000; ++n)
        {
            const double delta = d / 1000.0;
            const double catchUp = in.variableBytes * (n - 1) * delta / (2 * in.bandwidth) +
                                   in.costs.remote * (n - 1) / n + in.work * delta / (2 * in.rate);
            if (catchUp > in.deadlineSlack)
                continue;
            ++result.feasibleCount;
            const double maintenance = in.costs.local / delta + in.costs.remote / (n * delta);
            const double j = alreadyOn ? in.rate / in.work * maintenance + in.qOneSecond * catchUp
                                       : (1 - in.progress) * maintenance + in.pFinish * catchUp;
            Candidate candidate{d, n, j, catchUp};
            if (!result.best || std::tie(j, d, n) < std::tie(result.best->objective,
                                                             result.best->deltaPermille,
                                                             result.best->remoteEvery))
                result.best = candidate;
        }
    return result;
}

double
RecomputeCatchUp(const DecisionInput& in)
{
    Validate(in);
    return in.inputBytes / in.bandwidth + in.progress * in.work / in.rate;
}

double
InitializationTime(const DecisionInput& in, uint64_t legalStateBytes)
{
    Validate(in);
    return std::max(in.inputBytes / in.bandwidth, in.costs.local + legalStateBytes / in.bandwidth) +
           in.costs.remote;
}

bool
ShouldStartProtection(const DecisionInput& in, const Selection& selection, double init)
{
    Validate(in);
    if (!std::isfinite(init) || init < 0)
        throw std::invalid_argument("invalid initialization time");
    return selection.best && init < in.remainingSeconds &&
           in.costs.local + in.costs.remote + selection.best->objective <
               in.pFinish * RecomputeCatchUp(in);
}

CatchUp
EstimateCatchUp(const DecisionInput& in, bool on, double local, double remote)
{
    Validate(in);
    if (remote < 0 || local < remote || local > in.progress)
        throw std::invalid_argument("shadow must maintain 0 <= r <= l <= x");
    const double wait =
        on ? in.variableBytes * (local - remote) / in.bandwidth : in.inputBytes / in.bandwidth;
    const double executed =
        on ? in.work * (in.progress - local) + in.rate * in.costs.remote * (local > remote)
           : in.work * in.progress;
    return {wait + executed / in.rate, executed, in.rate * wait};
}
} // namespace ns3::compfrr
