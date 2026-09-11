/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PLACEMENT_LOAD_LEDGER_H
#define SATCOMPUTE_PLACEMENT_LOAD_LEDGER_H
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ns3::protection
{
/** Live ownership counts, distinct from cumulative assignment/acceptance totals. */
struct PlacementNodeLoad
{
    uint64_t activeBackup{}, activeRecovery{}, totalBackup{}, totalRecovery{}, peakBackup{},
        peakRecovery{}; ///< Exact integer counts, including zero-count nodes.
};

/** Transition audit in observed callback order, including repeated same-ns transitions. */
struct PlacementLoadEvent
{
    uint64_t taskId{};
    uint32_t node{};
    int64_t timeNs{};
    std::string event;
    PlacementNodeLoad load; ///< Node counts immediately after this ownership transition.
};

/** Pure ownership ledger. Callbacks observe real admission/release; never reserve resources. */
class PlacementLoadLedger
{
  public:
    void RegisterNode(uint32_t node); ///< Include idle/unselected nodes in concentration metrics.
    void Assignment(uint64_t task, uint32_t node, bool active, int64_t timeNs);
    ///< Count only established remote assignments; release is idempotent.
    void Recovery(uint64_t task, uint32_t node, bool active, int64_t timeNs);
    ///< Accepted/reserved/running until compute complete, failure or terminal cleanup.
    const PlacementNodeLoad& Get(uint32_t node) const; ///< Current causal ranking inputs.

    const std::map<uint32_t, PlacementNodeLoad>& Nodes() const
    {
        return m_nodes;
    }

    const std::vector<PlacementLoadEvent>& Events() const
    {
        return m_events;
    }

    bool Empty() const; ///< All ownership released, cumulative evidence retained.
    void WriteMetrics(const std::filesystem::path& directory) const; ///< Read-only end-of-run CSV.
  private:
    void Change(uint64_t task, uint32_t node, bool active, int64_t timeNs, bool recovery);
    std::map<uint32_t, PlacementNodeLoad> m_nodes;            ///< All configured compute nodes.
    std::map<uint64_t, uint32_t> m_assignments, m_recoveries; ///< Current task ownership only.
    std::vector<PlacementLoadEvent> m_events; ///< Actual transitions, no synthetic samples.
};
} // namespace ns3::protection
#endif
