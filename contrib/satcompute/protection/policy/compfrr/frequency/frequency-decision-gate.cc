/* SPDX-License-Identifier: GPL-2.0-only */
#include "frequency-decision-gate.h"

#include <stdexcept>

namespace ns3::protection
{
void FrequencyDecisionGate::Propose(const FrequencyDecision& decision, bool capacityRetry)
{
    const bool off = m_phase == ProtectionPhase::OFF;
    const bool sameTimeRetry = capacityRetry && off && decision.epochNs == m_lastEpoch &&
                               decision.epochNs > m_lastCapacityEpoch;
    if (m_proposal || (decision.epochNs <= m_lastEpoch && !sameTimeRetry) ||
        (capacityRetry && (!off || decision.epochNs <= m_lastCapacityEpoch)) || decision.phase != m_phase ||
        (!off && m_phase != ProtectionPhase::ON))
        throw std::invalid_argument("stale, duplicate or wrong-phase frequency proposal");
    if ((off && decision.action != FrequencyAction::NONE &&
         decision.action != FrequencyAction::START) ||
        (!off && decision.action == FrequencyAction::START))
        throw std::invalid_argument("frequency action does not match effective phase");
    if (decision.action == FrequencyAction::START || decision.action == FrequencyAction::UPDATE)
    {
        if (!decision.selected)
            throw std::invalid_argument("frequency proposal missing selected config");
        const auto config = decision.selected->config;
        if (config.deltaPermille < 10 || config.deltaPermille > 100 || !config.batchN ||
            config.batchN > 100 || config.batchN * config.deltaPermille > 1000)
            throw std::invalid_argument("frequency proposal outside search grid");
    }
    m_proposal = decision;
    if (capacityRetry) m_lastCapacityEpoch = decision.epochNs;
    m_lastEpoch = decision.epochNs;
}

bool FrequencyDecisionGate::Resolve(int64_t epochNs, bool currentFaultHit, bool primaryStillRunning)
{
    if (!m_proposal || m_proposal->epochNs != epochNs)
        throw std::invalid_argument("frequency resolution must match the pending fault epoch");
    const auto decision = *m_proposal;
    m_proposal.reset();
    if (currentFaultHit || !primaryStillRunning || decision.action == FrequencyAction::NONE)
        return false;
    if (decision.action == FrequencyAction::PAUSE)
    {
        m_paused = true;
        return true;
    }
    m_current = decision.selected->config;
    m_paused = false;
    if (decision.action == FrequencyAction::START)
        m_phase = ProtectionPhase::INITIALIZING;
    return true;
}

void FrequencyDecisionGate::InitializationCommitted()
{
    if (m_phase != ProtectionPhase::INITIALIZING || !m_current || m_proposal)
        throw std::invalid_argument("frequency ON requires physical initialization commit");
    m_phase = ProtectionPhase::ON;
}

void FrequencyDecisionGate::Stop(ProtectionPhase phase)
{
    if ((phase != ProtectionPhase::RECOVERING && phase != ProtectionPhase::DONE) ||
        (m_phase == ProtectionPhase::DONE && phase != ProtectionPhase::DONE))
        throw std::invalid_argument("frequency cannot revert terminal state or turn OFF");
    m_phase = phase;
    m_proposal.reset();
    m_paused = true;
}

std::optional<uint64_t> FrequencyDecisionGate::NextTarget(const TaskStateAdapter& layout,
                                                          uint64_t completedWork,
                                                          uint64_t lastTriggeredWork) const
{
    if (m_phase != ProtectionPhase::ON || m_paused || !m_current)
        return std::nullopt;
    return layout.Next(completedWork, lastTriggeredWork, m_current->deltaPermille);
}

uint32_t FrequencyDecisionGate::NewBatchRecordCount(uint64_t unbatchedValidRecords) const
{
    return m_phase == ProtectionPhase::ON && !m_paused && m_current &&
                   unbatchedValidRecords >= m_current->batchN
               ? m_current->batchN
               : 0;
}
} // namespace ns3::protection
