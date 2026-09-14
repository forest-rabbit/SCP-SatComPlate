/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-state.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection::checkbullet
{
namespace
{
/** Checked monotonic scheduling time, with no floating point conversion. */
int64_t After(int64_t now, int64_t delay)
{
    if (now < 0 || delay < 0 || now > std::numeric_limits<int64_t>::max() - delay)
        throw std::invalid_argument("invalid CB callback time");
    return now + delay;
}
} // namespace

CbState::CbState(const TaskDefinition& task, uint64_t attemptGeneration, uint32_t backupNode)
    : m_layout(task), m_taskId(task.taskId), m_generation(attemptGeneration),
      m_inputBytes(task.inputBytes), m_backupNode(backupNode),
      m_costs(GetProtectionCosts(m_layout.VariableBytes()))
{
    if (backupNode == task.computeNodeId)
        throw std::invalid_argument("CB backup cannot be the primary");
}

void
CbState::CheckTime(int64_t nowNs)
{
    if (nowNs < 0 || nowNs < m_lastEventNs)
        throw std::invalid_argument("CB callback clock moved backwards");
    m_lastEventNs = nowNs;
}

uint64_t
CbState::FullBytes(uint64_t work) const
{
    if (m_layout.Floor(work) != work)
        throw std::invalid_argument("CB full state must use a legal boundary");
    const auto variable = m_layout.StateBytes(work);
    if (variable > std::numeric_limits<uint64_t>::max() - m_layout.HeaderBytes())
        throw std::overflow_error("CB full-state byte overflow");
    return variable + m_layout.HeaderBytes();
}

CbRecord
CbState::Capture(uint64_t work, uint64_t actualWork, int64_t nowNs)
{
    if (m_stopped)
        throw std::invalid_argument("CB capture after stop");
    const auto previous = m_records.empty() ? 0 : m_records.rbegin()->second.key.toWork;
    if (work <= previous || work >= m_layout.Work() || work > actualWork ||
        actualWork > m_layout.Work() || m_layout.Floor(work) != work)
        throw std::invalid_argument("CB capture is not completed, legal, increasing work");
    const auto sequence = m_records.size() + 1;
    CbRecord row;
    row.key = {m_taskId, m_generation, sequence - 1, sequence, previous, work};
    row.bytes = sequence == 1 ? FullBytes(work) : m_layout.RecordBytes(previous, work);
    row.capturedNs = nowNs;
    row.generatedNs = After(nowNs, m_costs.localNs);
    CheckTime(nowNs);
    m_records.emplace(sequence, row);
    return row;
}

bool
CbState::Receive(const CbRecordKey& key, uint32_t holderNode, int64_t nowNs)
{
    const auto found = m_records.find(key.sequence);
    if (m_stopped || holderNode != m_backupNode || found == m_records.end() ||
        found->second.key != key || found->second.receivedNs >= 0)
        return false;
    auto& row = found->second;
    if (nowNs < row.generatedNs)
        throw std::invalid_argument("CB receipt before state generation");
    CheckTime(nowNs);
    row.receivedNs = nowNs;
    if (!m_pending)
        row.committedNs = nowNs;
    return true;
}

bool
CbState::ReceiveInput(uint32_t holderNode, uint64_t bytes, int64_t nowNs)
{
    if (m_stopped || holderNode != m_backupNode || bytes != m_inputBytes || m_inputReadyNs >= 0)
        return false;
    CheckTime(nowNs);
    m_inputReadyNs = nowNs;
    return true;
}

bool
CbState::CommitInitial(int64_t nowNs)
{
    if (m_stopped || !m_roots.empty() || m_records.empty() ||
        m_records.begin()->second.receivedNs < 0)
        return false;
    const auto& initial = m_records.begin()->second;
    if (nowNs < After(initial.receivedNs, m_costs.remoteNs))
        throw std::invalid_argument("CB initial root before cR completion");
    CheckTime(nowNs);
    m_roots.push_back({initial.key.sequence, initial.key.toWork, nowNs});
    return true;
}

CbSnapshot
CbState::Snapshot(int64_t nowNs, bool strict) const
{
    if (nowNs < 0)
        throw std::invalid_argument("negative CB snapshot time");
    const auto visible = [=](int64_t at) {
        return at >= 0 && (strict ? at < nowNs : at <= nowNs);
    };
    CbSnapshot out;
    out.taskId = m_taskId;
    out.attemptGeneration = m_generation;
    out.backupNode = m_backupNode;
    out.inputReady = visible(m_inputReadyNs);
    for (const auto& root : m_roots)
        if (visible(root.committedNs))
        {
            out.rootReady = true;
            out.rootWork = out.recoverableWork = root.work;
            out.rootSequence = root.sequence;
        }
    if (!out.rootReady)
        return out;
    auto previous = out.rootSequence;
    for (auto it = m_records.upper_bound(previous); it != m_records.end(); ++it)
    {
        const auto& row = it->second;
        if (!visible(row.committedNs) || row.key.baseVersion != previous ||
            row.key.fromWork != out.recoverableWork)
            break;
        out.logSequences.push_back(row.key.sequence);
        out.recoverableWork = row.key.toWork;
        previous = row.key.sequence;
    }
    return out;
}

CbSnapshot CbState::At(int64_t nowNs) const { return Snapshot(nowNs, false); }
CbSnapshot CbState::BeforeFault(int64_t faultNs) const { return Snapshot(faultNs, true); }

std::optional<int64_t>
CbState::BeginMerge(uint64_t throughSequence, int64_t nowNs)
{
    if (m_stopped || m_pending)
        return std::nullopt;
    const auto snapshot = At(nowNs);
    if (std::find(snapshot.logSequences.begin(), snapshot.logSequences.end(), throughSequence) ==
        snapshot.logSequences.end())
        return std::nullopt;
    const auto ready = After(nowNs, m_costs.remoteNs);
    CheckTime(nowNs);
    m_pending = PendingMerge{throughSequence, m_records.at(throughSequence).key.toWork, ready};
    return ready;
}

bool
CbState::CommitMerge(int64_t nowNs)
{
    if (m_stopped || !m_pending)
        return false;
    if (nowNs < m_pending->readyNs)
        throw std::invalid_argument("CB merge before cR completion");
    CheckTime(nowNs);
    m_roots.push_back({m_pending->sequence, m_pending->work, nowNs});
    m_pending.reset();
    for (auto& [sequence, row] : m_records)
        if (row.receivedNs >= 0 && row.committedNs < 0)
            row.committedNs = nowNs;
    return true;
}
} // namespace ns3::protection::checkbullet
