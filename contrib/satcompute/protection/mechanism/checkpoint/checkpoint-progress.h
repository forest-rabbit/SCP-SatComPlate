/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CHECKPOINT_PROGRESS_H
#define SATCOMPUTE_CHECKPOINT_PROGRESS_H
#include "../../common/task-state-adapter.h"
#include <map>

namespace ns3::protection
{
/** Valid state at a causal instant; initialized is independent of zero state bytes. */
struct CheckpointSnapshot
{
    bool initialized{};    ///< Initialization really committed.
    uint64_t localWork{};  ///< Highest contiguous local received prefix.
    uint64_t remoteWork{}; ///< Highest remote merged prefix.
};

/** Pure timing/progress contract, driven by explicit callbacks in G2, not a network simulator.
 * Captures immutable legal boundaries and keeps history for conservative same-ns faults.
 */
class CheckpointProgress
{
  public:
    /** Start initialization at a legal captured boundary. Layout must outlive this object. */
    CheckpointProgress(const TaskStateAdapter& layout, uint64_t capturedWork, int64_t startNs);
    /** Both init transfers completed; state arrival cannot precede cL generation. */
    int64_t ReceiveInitialization(int64_t baseReceivedNs, int64_t stateReceivedNs);
    /** Capture already completed legal work; return generation-complete time, not L1 validity. */
    int64_t Capture(uint64_t work, uint64_t actualCompletedWork, int64_t nowNs);
    /** Receiver-complete for one captured record; only contiguous receipts advance l. */
    bool ReceiveLocal(uint64_t work, int64_t nowNs);
    /** A contiguous remote batch arrived; return its cR completion time. One batch at a time. */
    int64_t ReceiveRemote(uint64_t work, int64_t nowNs);
    /** Explicit internal commit callback; returns the covered local-record boundary, if any. */
    std::optional<uint64_t> CommitRemote(int64_t nowNs);

    /** Current snapshot for normal observations. */
    CheckpointSnapshot Current() const
    {
        return m_current;
    }

    /** Fault uses only commits strictly before faultNs, regardless of same-ns callback order. */
    CheckpointSnapshot BeforeFault(int64_t faultNs) const;
    /** Cancel uncompleted operations; stale callbacks cannot create state. */
    void Stop();

  private:
    /** A captured increment is immutable, including its generation completion deadline. */
    struct LocalRecord
    {
        int64_t generatedNs; ///< Earliest time its transfer may start.
        bool received{};     ///< Receiver has all bytes.
    };

    /** Store a monotonic observation at the explicit event timestamp. */
    void Record(int64_t nowNs);
    const TaskStateAdapter& m_layout;        ///< Shared immutable layout.
    ProtectionCosts m_costs;                 ///< Single production cost source.
    uint64_t m_initialWork;                  ///< Initialization's immutable captured state.
    int64_t m_start;                         ///< Initialization capture time.
    int64_t m_lastEvent;                     ///< Reject callback time travel.
    uint64_t m_triggered;                    ///< Last captured L1 boundary.
    bool m_stopped{};                        ///< Terminal guard.
    bool m_initReceived{};                   ///< Exactly one initialization reception pair.
    CheckpointSnapshot m_current;            ///< Current valid prefix.
    std::map<uint64_t, LocalRecord> m_local; ///< Captured ordered records, not byte storage.
    std::optional<std::pair<int64_t, uint64_t>> m_pending; ///< Commit time and target WU.
    std::map<int64_t, CheckpointSnapshot> m_history; ///< Validity history, including same-ns state.
};
} // namespace ns3::protection
#endif
