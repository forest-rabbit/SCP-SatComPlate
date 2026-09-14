/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PEAK_QUOTA_LEDGER_H
#define SATCOMPUTE_PEAK_QUOTA_LEDGER_H
#include <cstdint>
#include <map>
#include <optional>

namespace ns3::protection
{
/** Per-task remote peak promises; actual objects are never counted a second time. */
class PeakQuotaLedger
{
  public:
    void Replace(uint64_t task, uint32_t node, uint64_t peakBytes);
    void Release(uint64_t task);
    /** Sum max(actual,quota), optionally omitting the replaced task's old quota only. */
    uint64_t Accounted(uint32_t node, const std::map<uint64_t, uint64_t>& actual,
                       std::optional<uint64_t> replacing = {}) const;
    bool Empty() const { return m_quotas.empty(); }
    /** Existing committed promise for this owner/node, without changing accounting. */
    std::optional<uint64_t> Peak(uint64_t task, uint32_t node) const
    {
        const auto it = m_quotas.find(task);
        return it != m_quotas.end() && it->second.first == node
            ? std::optional(it->second.second) : std::nullopt;
    }
  private:
    std::map<uint64_t, std::pair<uint32_t, uint64_t>> m_quotas; ///< Task -> fixed remote/peak.
};


} // namespace ns3::protection
#endif
