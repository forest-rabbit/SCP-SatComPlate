/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FREQUENCY_DECISION_GATE_H
#define SATCOMPUTE_FREQUENCY_DECISION_GATE_H
#include "compfrr-frequency-policy.h"

namespace ns3::protection
{
/** Per-task pure proposal/commit contract, not a second checkpoint runtime.
 * G2 supplies post-fault liveness and calls InitializationCommitted only after
 * N5A physical initialization. This object never samples faults, stores records,
 * sends transfers, or changes existing committed state or immutable batches.
 */
class FrequencyDecisionGate
{
  public:
    /** Stage one decision without changing current config or effective phase. */
    void Propose(const FrequencyDecision& decision, bool capacityRetry = false);
    /** Consume exactly one same-epoch proposal after fault execution/liveness resolution. */
    bool Resolve(int64_t epochNs, bool currentFaultHit, bool primaryStillRunning);
    /** External physical initialization commit, never caused by a policy proposal. */
    void InitializationCommitted();
    /** Stop policy work on recovery or terminal entry; no return to OFF is allowed. */
    void Stop(ProtectionPhase phase);
    /** Future-only legal target; never schedules or captures it. */
    std::optional<uint64_t> NextTarget(const TaskStateAdapter& layout,
                                       uint64_t completedWork,
                                       uint64_t lastTriggeredWork) const;
    /** Number of unbatched valid records to consume now; created batches are outside this API. */
    uint32_t NewBatchRecordCount(uint64_t unbatchedValidRecords) const;

    /** Effective phase, unchanged by merely proposing a decision. */
    ProtectionPhase Phase() const
    {
        return m_phase;
    }

    /** Last configuration actually committed after survival. */
    const std::optional<FrequencyConfiguration>& CurrentConfig() const
    {
        return m_current;
    }

    /** Whether new targets and batches are suspended without deleting state. */
    bool Paused() const
    {
        return m_paused;
    }

  private:
    ProtectionPhase m_phase{ProtectionPhase::OFF};   ///< Effective state only.
    std::optional<FrequencyConfiguration> m_current; ///< Committed configuration.
    std::optional<FrequencyDecision> m_proposal;     ///< No effective side effects before Resolve.
    int64_t m_lastEpoch{-1};                         ///< Reject duplicate or stale decisions.
    int64_t m_lastCapacityEpoch{-1}; ///< One fresh OFF/paused-ON resource decision per timestamp.
    bool m_paused{}; ///< Suppresses both new targets and new batches, never deletes state.
};
} // namespace ns3::protection
#endif
