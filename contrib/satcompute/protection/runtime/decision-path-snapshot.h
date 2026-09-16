/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_DECISION_PATH_SNAPSHOT_H
#define SATCOMPUTE_DECISION_PATH_SNAPSHOT_H
#include "../../traffic/network-transfer-engine.h"
#include "../policy/placement-policy.h"

namespace ns3::protection
{
/** Stack-owned synchronous decision only. Never keep across events or use for admission. */
class DecisionPathSnapshot
{
  public:
    /** Preview must be read-only; real transfers always re-admit through the shared engine. */
    explicit DecisionPathSnapshot(
        std::function<AdmissiblePathEstimate(uint32_t, uint32_t)> preview)
        : m_preview(std::move(preview))
    {
    }
    /** Memoize the complete result, including local delivery, path, rate and propagation. */
    const AdmissiblePathEstimate& Get(uint32_t source, uint32_t destination)
    {
        const auto key = std::pair{source, destination};
        auto found = m_paths.find(key);
        if (found == m_paths.end())
            found = m_paths.emplace(key, m_preview(source, destination)).first;
        return found->second;
    }
    /** Pair feasibility consumes the same full result later used for solver costs. */
    PlacementPathAvailability Availability(uint32_t source, uint32_t destination)
    {
        const auto& p = Get(source, destination);
        return {p.reachable, p.admissible, p.failureReason};
    }

  private:
    std::function<AdmissiblePathEstimate(uint32_t, uint32_t)> m_preview; ///< Borrowed live query.
    std::map<std::pair<uint32_t, uint32_t>, AdmissiblePathEstimate> m_paths; ///< Decision scope.
};
} // namespace ns3::protection
#endif
