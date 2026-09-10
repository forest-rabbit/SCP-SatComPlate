/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-types.h"
#include <stdexcept>

namespace ns3::protection
{
ExecutionAttempt::ExecutionAttempt(uint64_t taskId, uint32_t primaryNode, int64_t deadlineNs)
    : m_key{taskId, 0}, m_node(primaryNode), m_deadline(deadlineNs)
{
    if (!taskId || deadlineNs < 0)
        throw std::invalid_argument("attempt requires task ID and established deadline");
}

bool
ExecutionAttempt::AcceptRecovery(uint32_t node, bool healthyAndIdle, int64_t nowNs)
{
    if (nowNs < 0 || nowNs >= m_deadline || !healthyAndIdle || node == m_node ||
        m_role != AttemptRole::PRIMARY || m_stage != AttemptStage::RUNNING)
        return false;
    m_key.generation = 1;
    m_acceptedNs = nowNs;
    m_node = node;
    m_role = AttemptRole::RECOVERY;
    m_stage = AttemptStage::RECOVERING;
    return true;
}

bool
ExecutionAttempt::Owns(AttemptKey key) const
{
    return key == m_key && m_stage != AttemptStage::COMPLETED && m_stage != AttemptStage::FAILED;
}

bool
ExecutionAttempt::StartRecovery(AttemptKey key, int64_t nowNs)
{
    if (!Owns(key) || m_stage != AttemptStage::RECOVERING || nowNs < m_acceptedNs || nowNs >= m_deadline)
        return false;
    m_stage = AttemptStage::RUNNING_BACKUP;
    m_computeStartedNs = nowNs;
    return true;
}

bool
ExecutionAttempt::ImmuneToComputeFault() const
{
    return m_role == AttemptRole::RECOVERY &&
           (m_stage == AttemptStage::RECOVERING || m_stage == AttemptStage::RUNNING_BACKUP);
}

bool
ExecutionAttempt::Fail()
{
    if (!Owns(m_key))
        return false;
    m_stage = AttemptStage::FAILED;
    return true;
}

bool
ExecutionAttempt::ApplyFault(bool permanentSatelliteFault)
{
    if (permanentSatelliteFault)
        return Fail();
    if (ImmuneToComputeFault() || m_stage == AttemptStage::RESULT)
        return false;
    return Fail();
}

bool
ExecutionAttempt::CompleteCompute(AttemptKey key, int64_t nowNs)
{
    if (!Owns(key) || nowNs < m_computeStartedNs ||
        (m_stage != AttemptStage::RUNNING && m_stage != AttemptStage::RUNNING_BACKUP))
        return false;
    if (nowNs > m_deadline)
    {
        Fail();
        return false;
    }
    m_stage = AttemptStage::RESULT;
    return true;
}

bool
ExecutionAttempt::CompleteResult(AttemptKey key)
{
    if (!Owns(key) || m_stage != AttemptStage::RESULT)
        return false;
    m_stage = AttemptStage::COMPLETED;
    return true;
}

RecoveryPath
ChooseRecoveryPath(std::optional<int64_t> tail, std::optional<int64_t> redo)
{
    if ((tail && *tail < 0) || (redo && *redo < 0))
        throw std::invalid_argument("recovery estimate must be nonnegative");
    // A local delta chain alone cannot substitute for the remote recovery base.
    if (!redo)
        return RecoveryPath::RECOMPUTE;
    return tail && *tail < *redo ? RecoveryPath::TAIL : RecoveryPath::REMOTE_REDO;
}
} // namespace ns3::protection
