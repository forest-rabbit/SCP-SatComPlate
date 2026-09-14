/* SPDX-License-Identifier: GPL-2.0-only */
#include "peak-quota-ledger.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
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
}
void PeakQuotaLedger::Replace(uint64_t task, uint32_t node, uint64_t bytes)
{
    Require(task != 0, "N5C quota requires a task");
    const auto found = m_quotas.find(task);
    Require(found == m_quotas.end() || found->second.first == node, "N5C ON cannot replace remote");
    m_quotas[task] = {node, bytes};
}
void PeakQuotaLedger::Release(uint64_t task) { m_quotas.erase(task); }
uint64_t PeakQuotaLedger::Accounted(uint32_t node, const std::map<uint64_t, uint64_t>& actual,
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
} // namespace ns3::protection
