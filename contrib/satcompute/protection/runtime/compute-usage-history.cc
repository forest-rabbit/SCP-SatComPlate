/* SPDX-License-Identifier: GPL-2.0-only */
#include "compute-usage-history.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
ComputeUsageHistory::Prefix ComputeUsageHistory::Advance(Prefix p, int64_t time)
{
    if (time < p.timeNs) throw std::logic_error("compute history moved backwards");
    const auto duration = static_cast<uint64_t>(time - p.timeNs);
    if (p.activity == Activity::NORMAL) p.normalNs += duration;
    if (p.activity == Activity::RECOVERY) p.recoveryNs += duration;
    p.timeNs = time;
    return p;
}
void ComputeUsageHistory::Observe(uint32_t node, int64_t time, Activity activity)
{
    if (time < 0) throw std::invalid_argument("negative compute observation time");
    const auto previous = m_lastObservedNs.find(node);
    if (previous != m_lastObservedNs.end() && time < previous->second)
        throw std::invalid_argument("out-of-order compute observation");
    m_lastObservedNs[node] = time;
    auto& events = m_events[node];
    if (events.empty()) events.push_back({});
    auto next = Advance(events.back(), time);
    next.activity = activity;
    if (time == events.back().timeNs) events.back() = next;
    else if (activity != events.back().activity) events.push_back(next);
}
ComputeUsageHistory::Prefix ComputeUsageHistory::At(uint32_t node, int64_t time) const
{
    const auto found = m_events.find(node);
    if (found == m_events.end()) throw std::invalid_argument("unobserved compute node");
    const auto& events = found->second;
    const auto after = std::upper_bound(events.begin(), events.end(), time,
        [](int64_t t, const Prefix& p) { return t < p.timeNs; });
    if (after == events.begin()) throw std::invalid_argument("query before compute observation");
    return Advance(*std::prev(after), time);
}
ComputeUsageWindow ComputeUsageHistory::Query(uint32_t node, int64_t begin, int64_t end,
                                             int64_t asOf, uint64_t exposure) const
{
    if (begin < 0 || end < begin || asOf < end || exposure > static_cast<uint64_t>(asOf))
        throw std::invalid_argument("invalid or future compute history window");
    const auto first = At(node, begin), last = At(node, end);
    const auto liveEnd = std::min(static_cast<uint64_t>(end), exposure);
    ComputeUsageWindow out{last.normalNs - first.normalNs, last.recoveryNs - first.recoveryNs,
                          liveEnd > static_cast<uint64_t>(begin) ? liveEnd - begin : 0};
    if (out.normalNs + out.recoveryNs > out.exposureNs)
        throw std::logic_error("actual compute history exceeds observed survival exposure");
    return out;
}
} // namespace ns3::protection
