/* SPDX-License-Identifier: GPL-2.0-only */
#include "placement-load-ledger.h"

#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
void PlacementLoadLedger::RegisterNode(uint32_t node)
{
    m_nodes.try_emplace(node);
}

const PlacementNodeLoad& PlacementLoadLedger::Get(uint32_t node) const
{
    return m_nodes.at(node);
}

bool PlacementLoadLedger::Empty() const
{
    return m_assignments.empty() && m_recoveries.empty();
}

void PlacementLoadLedger::Assignment(uint64_t task, uint32_t node, bool active, int64_t time)
{
    Change(task, node, active, time, false);
}

void PlacementLoadLedger::Recovery(uint64_t task, uint32_t node, bool active, int64_t time)
{
    Change(task, node, active, time, true);
}

void PlacementLoadLedger::Change(uint64_t task,
                                 uint32_t node,
                                 bool active,
                                 int64_t time,
                                 bool recovery)
{
    auto& owners = recovery ? m_recoveries : m_assignments;
    if (!active && !owners.contains(task))
        return;
    if (time < 0 || (!m_events.empty() && time < m_events.back().timeNs))
        throw std::logic_error("placement load time moved backwards");
    auto& load = m_nodes.at(node);
    if (active && !owners.emplace(task, node).second)
        throw std::logic_error("duplicate live placement ownership");
    if (!active)
    {
        if (owners.at(task) != node)
            throw std::logic_error("placement ownership released on wrong node");
        owners.erase(task);
    }
    auto& count = recovery ? load.activeRecovery : load.activeBackup;
    auto& total = recovery ? load.totalRecovery : load.totalBackup;
    auto& peak = recovery ? load.peakRecovery : load.peakBackup;
    if (active)
    {
        ++count;
        ++total;
        peak = std::max(peak, count);
    }
    else
    {
        if (!count)
            throw std::logic_error("placement load underflow");
        --count;
    }
    m_events.push_back({task,
                        node,
                        time,
                        recovery ? (active ? "RECOVERY_ACCEPTED" : "RECOVERY_RELEASED")
                                 : (active ? "ASSIGNMENT_ESTABLISHED" : "ASSIGNMENT_RELEASED"),
                        load});
}
} // namespace ns3::protection
