/* SPDX-License-Identifier: GPL-2.0-only */
#include "checkpoint-progress.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
/** Checked nonnegative timestamp plus duration. */
int64_t
After(int64_t time, int64_t duration)
{
    if (time < 0 || duration < 0 || duration > std::numeric_limits<int64_t>::max() - time)
        throw std::invalid_argument("checkpoint time outside ns range");
    return time + duration;
}
} // namespace

CheckpointProgress::CheckpointProgress(const TaskStateAdapter& layout,
                                       uint64_t capturedWork,
                                       int64_t startNs)
    : m_layout(layout), m_costs(GetProtectionCosts(layout.VariableBytes())),
      m_initialWork(capturedWork), m_start(startNs), m_lastEvent(startNs), m_triggered(capturedWork)
{
    if (startNs < 0 || layout.Floor(capturedWork) != capturedWork)
        throw std::invalid_argument("initialization capture must be a legal boundary");
    After(startNs, m_costs.localNs);
}

int64_t
CheckpointProgress::ReceiveInitialization(int64_t baseReceivedNs, int64_t stateReceivedNs)
{
    if (m_stopped || m_initReceived || baseReceivedNs < m_start ||
        stateReceivedNs < After(m_start, m_costs.localNs))
        throw std::invalid_argument("invalid initialization reception");
    const auto received = std::max(baseReceivedNs, stateReceivedNs);
    const auto at = After(received, m_costs.remoteNs);
    m_pending = std::pair{at, m_initialWork};
    m_initReceived = true;
    m_lastEvent = received;
    return at;
}

int64_t
CheckpointProgress::Capture(uint64_t work, uint64_t actualCompletedWork, int64_t nowNs)
{
    if (m_stopped || !m_current.initialized || nowNs < m_lastEvent ||
        actualCompletedWork > m_layout.Work() || work > actualCompletedWork ||
        work <= m_triggered || m_layout.Floor(work) != work)
        throw std::invalid_argument("checkpoint capture must be new, legal and already computed");
    const auto at = After(nowNs, m_costs.localNs);
    m_local.emplace(work, LocalRecord{at, false});
    m_triggered = work;
    m_lastEvent = nowNs;
    return at;
}

void
CheckpointProgress::Record(int64_t nowNs)
{
    m_lastEvent = nowNs;
    m_history[nowNs] = m_current;
}

bool
CheckpointProgress::ReceiveLocal(uint64_t work, int64_t nowNs)
{
    if (m_stopped)
        return false;
    auto it = m_local.find(work);
    if (it == m_local.end() || it->second.received)
        return false;
    if (nowNs < m_lastEvent || nowNs < it->second.generatedNs)
        throw std::invalid_argument("L1 reception precedes generation or previous event");
    it->second.received = true;
    for (auto next = m_local.upper_bound(m_current.localWork);
         next != m_local.end() && next->second.received;
         ++next)
        m_current.localWork = next->first;
    Record(nowNs);
    return true;
}

int64_t
CheckpointProgress::ReceiveRemote(uint64_t work, int64_t nowNs)
{
    if (m_stopped || !m_current.initialized || m_pending || nowNs < m_lastEvent ||
        work <= m_current.remoteWork || work > m_current.localWork || !m_local.contains(work))
        throw std::invalid_argument("remote batch requires a received contiguous local prefix");
    const auto at = After(nowNs, m_costs.remoteNs);
    m_pending = std::pair{at, work};
    m_lastEvent = nowNs;
    return at;
}

std::optional<uint64_t>
CheckpointProgress::CommitRemote(int64_t nowNs)
{
    if (m_stopped || !m_pending)
        return std::nullopt;
    if (nowNs < m_pending->first || nowNs < m_lastEvent)
        throw std::invalid_argument("remote commit precedes reception plus cR");
    const auto work = m_pending->second;
    m_current.remoteWork = work;
    if (!m_current.initialized)
    {
        m_current.initialized = true;
        m_current.localWork = work;
    }
    m_local.erase(m_local.begin(), m_local.upper_bound(work));
    m_pending.reset();
    Record(nowNs);
    return work;
}

CheckpointSnapshot
CheckpointProgress::BeforeFault(int64_t faultNs) const
{
    if (faultNs < m_start)
        throw std::invalid_argument("fault predates protection context");
    const auto it = m_history.lower_bound(faultNs);
    return it == m_history.begin() ? CheckpointSnapshot{} : std::prev(it)->second;
}

void
CheckpointProgress::Stop()
{
    m_stopped = true;
    m_pending.reset();
    m_local.clear();
}
} // namespace ns3::protection
