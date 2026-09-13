/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-policy.h"
#include "../../../../task/compute-service.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ns3::protection::checkbullet
{
CbInterval
SolveInterval(const TaskStateAdapter& layout, uint64_t primaryRate, double mtbfSeconds)
{
    if (std::isnan(mtbfSeconds) || mtbfSeconds <= 0)
        throw std::invalid_argument("CB MTBF must be positive or positive infinity");
    const auto cost = GetProtectionCosts(layout.VariableBytes());
    CbInterval out;
    out.mtbfSeconds = mtbfSeconds;
    out.referenceCostSeconds = (cost.localNs + cost.remoteNs) / 1e9;
    out.taskSeconds = ComputeService::CalculateServiceTimeNs(layout.Work(), primaryRate) / 1e9;
    out.intervalSeconds = std::sqrt(2 * mtbfSeconds * out.referenceCostSeconds);
    out.rawFraction = out.intervalSeconds / out.taskSeconds;
    if (out.rawFraction >= 1)
    {
        out.reason = "INTERVAL_EXCEEDS_TASK";
        return out;
    }
    out.deltaPermille = std::max(1u, static_cast<uint32_t>(std::floor(1000 * out.rawFraction)));
    uint64_t previous = 0;
    for (uint32_t nominal = out.deltaPermille; nominal < 1000; nominal += out.deltaPermille)
    {
        // Anchor each target to the original cadence; rounding must not accumulate drift.
        const auto target = layout.Next(0, 0, nominal);
        if (target && *target > previous && *target < layout.Work())
        {
            out.targets.push_back({nominal, *target});
            previous = *target;
        }
    }
    out.reason = out.targets.empty() ? "NO_LEGAL_CHECKPOINT" : "PERIODIC";
    return out;
}

int64_t
CbRestoreCosts::Duration(uint64_t count) const
{
    if (baseNs < 0 || readPerLogNs < 0 || mergeNs < 0)
        throw std::invalid_argument("CB state costs cannot be negative");
    const unsigned __int128 duration = static_cast<uint64_t>(baseNs) +
        static_cast<unsigned __int128>(count) * static_cast<uint64_t>(readPerLogNs) +
        static_cast<unsigned __int128>(linearMerge ? count : uint64_t(count != 0)) *
            static_cast<uint64_t>(mergeNs);
    if (duration > std::numeric_limits<int64_t>::max())
        throw std::overflow_error("CB state reconstruction duration overflow");
    return static_cast<int64_t>(duration);
}

CbThreshold
SolveThreshold(uint64_t quotaBytes, uint64_t heldBytes,
               const std::vector<uint64_t>& logBytes, int64_t restoreBudgetNs,
               const CbRestoreCosts& costs, std::optional<uint64_t> implementationCap)
{
    if (restoreBudgetNs < 0)
        throw std::invalid_argument("CB restore budget cannot be negative");
    costs.Duration(0); // Validate even when no remaining event exists.
    CbThreshold out;
    out.naturalLimit = logBytes.size();
    out.implementationLimit = implementationCap.value_or(out.naturalLimit);
    // The nonempty intercept and slope allow fixed as well as linear merge fixtures.
    const unsigned __int128 intercept = static_cast<uint64_t>(costs.baseNs) +
        static_cast<unsigned __int128>(costs.linearMerge ? 0 : costs.mergeNs);
    const unsigned __int128 slope = static_cast<uint64_t>(costs.readPerLogNs) +
        static_cast<unsigned __int128>(costs.linearMerge ? costs.mergeNs : 0);
    if (intercept > static_cast<uint64_t>(restoreBudgetNs))
        out.recoveryLimit = 0;
    else if (slope)
        out.recoveryLimit = static_cast<uint64_t>((restoreBudgetNs - intercept) / slope);

    unsigned __int128 occupied = heldBytes;
    if (occupied <= quotaBytes)
        for (const auto bytes : logBytes)
        {
            occupied += bytes;
            if (occupied > quotaBytes)
                break;
            ++out.storageLimit;
        }
    out.value = std::min({out.naturalLimit, out.storageLimit, out.implementationLimit,
                          out.recoveryLimit.value_or(std::numeric_limits<uint64_t>::max())});
    out.feasible = out.value != 0;
    out.recoveryBinds = out.recoveryLimit && *out.recoveryLimit == out.value;
    out.storageBinds = out.storageLimit == out.value && out.storageLimit < out.naturalLimit;
    out.capBinds = out.implementationLimit == out.value &&
                   out.implementationLimit < out.naturalLimit;
    out.reason = !out.naturalLimit ? "NO_REMAINING_LOGS" :
        !out.feasible ? "CHECKPOINT_INFEASIBLE" :
        out.recoveryBinds ? "RESTORE_BUDGET" : out.storageBinds ? "STORAGE_QUOTA" :
        out.capBinds ? "IMPLEMENTATION_CAP" : "REMAINING_LEGAL_EVENTS";
    return out;
}

std::map<uint64_t, uint64_t>
ShareStorage(uint64_t capacity, const std::map<uint64_t, uint64_t>& ownerOccupancy)
{
    uint64_t free = capacity;
    for (const auto& [owner, used] : ownerOccupancy)
    {
        if (!owner || used > free)
            throw std::invalid_argument("invalid CB owner or aggregate storage occupancy");
        free -= used;
    }
    std::map<uint64_t, uint64_t> shares;
    if (ownerOccupancy.empty())
        return shares;
    const auto each = free / ownerOccupancy.size();
    auto remainder = free % ownerOccupancy.size();
    for (const auto& [owner, used] : ownerOccupancy)
    {
        shares[owner] = used + each + (remainder != 0);
        if (remainder)
            --remainder;
    }
    return shares;
}
} // namespace ns3::protection::checkbullet
