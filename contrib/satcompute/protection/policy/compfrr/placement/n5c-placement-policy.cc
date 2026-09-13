/* SPDX-License-Identifier: GPL-2.0-only */
#include "n5c-placement-policy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ns3::protection
{
namespace
{
void Require(bool value, const char* message)
{
    if (!value) throw std::invalid_argument(message);
}
uint64_t Add(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
        throw std::overflow_error("N5C storage accounting overflow");
    return a + b;
}
void Validate(const N5cForecast& f)
{
    const auto& in = f.input;
    for (auto x : {in.work, in.variableBytes, in.inputBytes, in.progress, in.primaryRate,
                   in.recoveryRate, in.backupBandwidth, in.inputBandwidth})
        Require(std::isfinite(x) && x >= 0, "invalid N5C resource scalar");
    Require(f.taskId && in.work > 0 && in.primaryRate > 0 && in.recoveryRate > 0 &&
            in.backupBandwidth > 0 && (in.inputPolicy == InputStagingPolicy::EAGER ||
                                      f.inputLocal || in.inputBandwidth > 0) &&
            in.progress <= 1 && in.risk.epochNs >= 0 && in.deadlineNs >= 0 &&
            f.config.deltaPermille >= 10 && f.config.deltaPermille <= 100 &&
            f.config.batchN && f.config.batchN <= 1000 / f.config.deltaPermille &&
            in.costs.remoteNs >= 0, "invalid N5C forecast domain");
}
double Remaining(const N5cForecast& f, int64_t at)
{
    const auto& in = f.input;
    const auto progress = std::min(1.0, in.progress +
        in.primaryRate * ((at - in.risk.epochNs) / 1e9) / in.work);
    return in.work * (1 - progress) / in.recoveryRate;
}
} // namespace

N5cVariant ParseN5cVariant(const std::string& name)
{
    if (name == "full") return N5cVariant::FULL;
    if (name == "noR") return N5cVariant::NO_R;
    if (name == "noU") return N5cVariant::NO_U;
    if (name == "noM") return N5cVariant::NO_M;
    throw std::invalid_argument("unknown N5C ablation");
}
const char* N5cVariantName(N5cVariant v)
{
    switch (v)
    {
    case N5cVariant::FULL: return "full";
    case N5cVariant::NO_R: return "noR";
    case N5cVariant::NO_U: return "noU";
    case N5cVariant::NO_M: return "noM";
    }
    throw std::invalid_argument("invalid N5C variant");
}

double N5cCatchSeconds(const N5cForecast& f)
{
    Validate(f);
    const auto& in = f.input;
    const double d = f.config.deltaPermille / 1000.0, n = f.config.batchN;
    const double input = in.inputPolicy == InputStagingPolicy::DEFERRED && !f.inputLocal
        ? in.inputBytes / in.inputBandwidth : 0;
    return input + in.variableBytes * (n - 1) * d / (2 * in.backupBandwidth) +
        (in.costs.remoteNs / 1e9) * (n - 1) / n + in.work * d / (2 * in.recoveryRate);
}
double N5cBudgetSeconds(const N5cForecast& f, int64_t at)
{
    Validate(f);
    Require(at >= f.input.risk.epochNs, "N5C queried a past recovery time");
    return (f.input.deadlineNs - at) / 1e9 - Remaining(f, at);
}

std::vector<N5cOccupancyWindow> N5cRecoveryWindows(const N5cForecast& f)
{
    const auto catchup = N5cCatchSeconds(f);
    std::vector<N5cOccupancyWindow> out;
    long double survival = 1;
    int64_t previous = -1;
    for (const auto& step : f.input.risk.futureSteps)
    {
        const auto q = step.combinedStepFailureProbability;
        Require(std::isfinite(q) && q >= 0 && q <= 1 && step.targetTimeNs > previous &&
                step.targetTimeNs >= f.input.risk.epochNs, "invalid N5C canonical risk trajectory");
        previous = step.targetTimeNs;
        const auto mass = survival * q;
        survival *= 1 - static_cast<long double>(q);
        // Always consume pre-ready survival; filtering is not a new probability process.
        if (!mass || !f.dependenciesAvailable || step.targetTimeNs <= f.readyAfterNs ||
            catchup > N5cBudgetSeconds(f, step.targetTimeNs) ||
            Remaining(f, step.targetTimeNs) <= 0)
            continue;
        const long double duration = std::ceil((catchup + Remaining(f, step.targetTimeNs)) * 1e9L);
        Require(std::isfinite(duration) && duration > 0 &&
                duration <= std::numeric_limits<int64_t>::max() - step.targetTimeNs,
                "N5C recovery occupancy overflow");
        out.push_back({step.targetTimeNs, step.targetTimeNs + static_cast<int64_t>(duration), mass});
    }
    return out;
}

N5cScore ScoreN5cCandidate(const N5cCandidate& c, N5cVariant variant)
{
    N5cScore out;
    out.remoteNode = c.remoteNode;
    out.propagationNs = c.propagationNs;
    out.reason = c.rejection;
    if (!out.reason.empty()) return out;
    Require(c.propagationNs >= 0 && c.remoteNode != c.demand.primaryNode,
            "invalid N5C remote or propagation");
    out.catchSeconds = N5cCatchSeconds(c.demand);
    out.budgetSeconds = N5cBudgetSeconds(c.demand, c.demand.input.risk.epochNs);
    if (!c.demand.dependenciesAvailable) out.reason = "DEPENDENCY_UNAVAILABLE";
    else if (out.catchSeconds > out.budgetSeconds) out.reason = "DEADLINE_INFEASIBLE";
    else if (!c.capacityBytes || c.accountedBytes > c.capacityBytes ||
             c.additionalQuotaBytes > c.capacityBytes - c.accountedBytes)
        out.reason = "STORAGE_QUOTA_INFEASIBLE";
    if (!out.reason.empty()) return out;
    const auto busy = Add(c.normalBusyNs, c.recoveryBusyNs);
    Require(busy <= c.exposureNs, "N5C execution exceeds survival exposure");
    out.historyUnavailable = c.exposureNs == 0;
    out.historicalUtilization = c.exposureNs ? static_cast<double>(busy) / c.exposureNs : 0;
    out.storagePressure = static_cast<double>(Add(c.accountedBytes, c.additionalQuotaBytes)) /
                         c.capacityBytes;
    const auto own = N5cRecoveryWindows(c.demand);
    std::vector<std::vector<N5cOccupancyWindow>> peers;
    std::set<uint32_t> primaries{c.demand.primaryNode};
    for (const auto& f : c.peers)
    {
        Require(f.input.risk.epochNs == c.demand.input.risk.epochNs &&
                primaries.insert(f.primaryNode).second, "N5C peers not independent active primaries");
        peers.push_back(N5cRecoveryWindows(f));
    }
    long double demand = 0, weighted = 0;
    for (const auto& w : own)
    {
        long double noConflict = 1;
        for (const auto& windows : peers)
        {
            long double g = 0;
            for (const auto& peer : windows)
                if (peer.startNs <= w.startNs && w.startNs < peer.endNs) g += peer.mass;
            Require(g >= 0 && g <= 1 + 1e-15L, "invalid N5C peer conflict mass");
            noConflict *= 1 - std::min(1.L, g); // Floating sum roundoff only.
        }
        demand += w.mass;
        weighted += w.mass * (1 - noConflict);
    }
    out.demandProbability = static_cast<double>(demand);
    out.weightedConflict = static_cast<double>(weighted);
    out.noPredictedDemand = demand == 0;
    out.recoveryConflict = demand > 0 ? static_cast<double>(weighted / demand) : 0;
    out.peerCount = peers.size();
    out.windowCount = own.size();
    const std::vector<std::pair<const char*, double>> dimensions{
        {"RECOVERY_CONFLICT", variant == N5cVariant::NO_R ? -1 : out.recoveryConflict},
        {"COMPUTE_HISTORY", variant == N5cVariant::NO_U ? -1 : out.historicalUtilization},
        {"STORAGE", variant == N5cVariant::NO_M ? -1 : out.storagePressure}};
    out.bottleneck = -1;
    for (const auto& [name, value] : dimensions)
        if (value > out.bottleneck) { out.bottleneck = value; out.dominant = name; }
    out.feasible = true;
    out.reason = "FEASIBLE";
    return out;
}

void N5cQuotaLedger::Replace(uint64_t task, uint32_t node, uint64_t bytes)
{
    Require(task != 0, "N5C quota requires a task");
    const auto found = m_quotas.find(task);
    Require(found == m_quotas.end() || found->second.first == node, "N5C ON cannot replace remote");
    m_quotas[task] = {node, bytes};
}
void N5cQuotaLedger::Release(uint64_t task) { m_quotas.erase(task); }
uint64_t N5cQuotaLedger::Accounted(uint32_t node, const std::map<uint64_t, uint64_t>& actual,
                                 std::optional<uint64_t> replacing) const
{
    auto perTask = actual;
    for (const auto& [task, quota] : m_quotas)
        if (quota.first == node && replacing != task)
            perTask[task] = std::max(perTask[task], quota.second);
    uint64_t total = 0;
    for (const auto& [task, bytes] : perTask) total = Add(total, bytes);
    return total;
}
N5cSelection N5cPlacementPolicy::SelectRemote(const std::vector<N5cCandidate>& candidates) const
{
    N5cSelection out;
    std::set<uint32_t> nodes;
    for (const auto& c : candidates)
    {
        Require(nodes.insert(c.remoteNode).second, "duplicate N5C remote candidate");
        out.scores.push_back(ScoreN5cCandidate(c, m_variant));
    }
    std::vector<const N5cScore*> ranked;
    for (const auto& score : out.scores) if (score.feasible) ranked.push_back(&score);
    std::sort(ranked.begin(), ranked.end(), [](const auto* a, const auto* b) {
        return std::tie(a->bottleneck, a->propagationNs, a->remoteNode) <
               std::tie(b->bottleneck, b->propagationNs, b->remoteNode);
    });
    out.feasibleCount = ranked.size();
    if (ranked.empty()) return out;
    out.remoteNode = ranked.front()->remoteNode;
    if (ranked.size() > 1 && ranked[0]->bottleneck == ranked[1]->bottleneck)
        out.tieBreak = ranked[0]->propagationNs == ranked[1]->propagationNs ? "STABLE_NODE_ID" : "PROPAGATION";
    return out;
}
void N5cPlacementPolicy::RankBackupNodes(std::vector<uint32_t>&, const PlacementContext&) const
{
    throw std::logic_error("N5C is CompFRR designated-remote placement, not recovery ranking");
}
} // namespace ns3::protection
