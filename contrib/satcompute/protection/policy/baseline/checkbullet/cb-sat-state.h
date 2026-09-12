/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CB_SAT_STATE_H
#define SATCOMPUTE_CB_SAT_STATE_H
#include "cb-sat-policy.h"
#include <map>

namespace ns3::protection::checkbullet
{
/** Immutable lineage. A merged root adopts the sequence of its last consumed log. */
struct CbRecordKey
{
    uint64_t taskId{}, attemptGeneration{}, baseVersion{}, sequence{}, fromWork{}, toWork{};
    bool operator==(const CbRecordKey&) const = default;
};

/** A capture is not a receipt; a receipt during a merge is not a committed new prefix. */
struct CbRecord
{
    CbRecordKey key;
    uint64_t bytes{};
    int64_t capturedNs{}, generatedNs{}, receivedNs{-1}, committedNs{-1};
};

/** Only one declared backup is a state source. INPUT is independent of the root/log chain. */
struct CbSnapshot
{
    uint64_t taskId{}, attemptGeneration{}, rootWork{}, recoverableWork{}, rootSequence{};
    uint32_t backupNode{};
    bool inputReady{}, rootReady{};
    std::vector<uint64_t> logSequences;
    /** Complete data dependencies, not merely nonzero progress. */
    bool Protected() const { return inputReady && rootReady; }
};

/** Pure CB lineage and causal history. Never reads global storage, faults or other nodes. */
class CbState
{
  public:
    /** Bind a task and one actual backup; layout is copied, not borrowed from a temporary. */
    CbState(const TaskDefinition& task, uint64_t attemptGeneration, uint32_t backupNode);
    /** Capture full state once, then legal deltas; no terminal or speculative progress. */
    CbRecord Capture(uint64_t work, uint64_t actualWork, int64_t nowNs);
    /** Validate the exact capture key and receiver before accepting a complete receipt. */
    bool Receive(const CbRecordKey& key, uint32_t holderNode, int64_t nowNs);
    /** Record complete immutable INPUT; its byte count is never progress-dependent. */
    bool ReceiveInput(uint32_t holderNode, uint64_t bytes, int64_t nowNs);
    /** Finish the initial state cR; full INPUT may still be in flight. */
    bool CommitInitial(int64_t nowNs);
    /** Lock an already contiguous prefix; returns cR completion, not immediate validity. */
    std::optional<int64_t> BeginMerge(uint64_t throughSequence, int64_t nowNs);
    /** Commit one locked merge, then admit any received queued records. */
    bool CommitMerge(int64_t nowNs);
    /** Strict-before snapshot used for faults, independent of equal-ns callback order. */
    CbSnapshot BeforeFault(int64_t faultNs) const;
    /** Inclusive current observation for ordinary checkpoint maintenance. */
    CbSnapshot At(int64_t nowNs) const;
    /** Keep history but disallow late captures, receipts and commits. */
    void Stop() { m_stopped = true; }
    /** Full original INPUT, unchanged after every capture and merge. */
    uint64_t InputBytes() const { return m_inputBytes; }
    /** Exact variable state plus one full-record header; never includes INPUT. */
    uint64_t FullBytes(uint64_t work) const;
    /** Read-only evidence of captured objects and their receipt/commit times. */
    const std::map<uint64_t, CbRecord>& Records() const { return m_records; }
    const TaskStateAdapter& Layout() const { return m_layout; }

  private:
    struct Root
    {
        uint64_t sequence{}, work{};
        int64_t committedNs{};
    };
    struct PendingMerge
    {
        uint64_t sequence{}, work{};
        int64_t readyNs{};
    };
    /** Update callback clock; rewind is a programmer error, not a network failure. */
    void CheckTime(int64_t nowNs);
    CbSnapshot Snapshot(int64_t nowNs, bool strict) const;
    TaskStateAdapter m_layout;
    uint64_t m_taskId{}, m_generation{}, m_inputBytes{};
    uint32_t m_backupNode{};
    ProtectionCosts m_costs;
    int64_t m_inputReadyNs{-1}, m_lastEventNs{-1};
    bool m_stopped{};
    std::map<uint64_t, CbRecord> m_records;
    std::vector<Root> m_roots;
    std::optional<PendingMerge> m_pending;
};
} // namespace ns3::protection::checkbullet
#endif
