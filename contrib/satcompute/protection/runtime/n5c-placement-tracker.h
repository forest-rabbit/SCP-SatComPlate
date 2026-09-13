/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_N5C_PLACEMENT_TRACKER_H
#define SATCOMPUTE_N5C_PLACEMENT_TRACKER_H
#include "../policy/compfrr/placement/n5c-placement-policy.h"
#include "../mechanism/checkpoint/checkpoint-manager.h"
#include "../../fault/runtime/fault-model-engine.h"
#include "compute-usage-history.h"
#include <filesystem>

namespace ns3::protection
{
/** One START-only spatial proposal and its post-fault admission result. */
struct N5cDecisionTrace
{
    uint64_t taskId{};
    int64_t timeNs{};
    std::string trigger;
    PlacementDecision reference;
    FrequencyInput referenceInput;
    FrequencyConfiguration config;
    N5cSelection selection;
    std::vector<N5cCandidate> candidates; ///< Scalar snapshots only after recording.
    bool committed{};
    std::string resolution{"PENDING"};
};

/** Exact assignment-time and physical backup-storage byte-time, including zero nodes. */
struct N5cNodeObservation
{
    uint64_t assignments{}, active{}, peakActive{}, storageBytes{}, peakStorage{};
    int64_t assignmentAtNs{}, storageAtNs{};
    unsigned __int128 assignmentNs{}, storageByteNs{}; ///< Exact integrals beyond uint64 byte-ns range.
};

/** Causal ledger/diagnostics only. Never owns compute, transfers, fault samples or Frequency. */
class N5cPlacementTracker
{
  public:
    N5cPlacementTracker(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults,
                        CheckpointManager& manager, int64_t stopNs, N5cVariant variant,
                        bool spatialDiagnostics = true);
    ~N5cPlacementTracker();
    void FillResources(N5cCandidate& candidate, int64_t remainingTimeNs) const;
    uint64_t FreeFor(uint32_t node, uint64_t replacingTask) const;
    uint64_t PeakFor(uint32_t node, uint64_t task, uint64_t additional) const;
    bool CanCommit(uint64_t task, uint32_t node, uint64_t peak) const;
    void CommitQuota(uint64_t task, uint32_t node, uint64_t peak);
    void ReleaseQuota(uint64_t task);
    void Assignment(uint64_t task, uint32_t node, bool active);
    void Initialized(uint64_t task);
    std::optional<int64_t> ReadyAfter(uint64_t task) const;
    size_t Record(N5cDecisionTrace trace);
    void Resolve(size_t index, bool committed, const std::string& reason);
    const N5cDecisionTrace& Decision(size_t index) const { return m_decisions.at(index); }
    const std::vector<N5cDecisionTrace>& Decisions() const { return m_decisions; }
    const std::map<uint32_t, N5cNodeObservation>& Nodes() const { return m_nodes; }
    bool QuotasEmpty() const { return m_quotas.Empty(); }
    void Finalize();
    void Write(const std::filesystem::path& directory) const;
  private:
    void ObserveStorage(uint32_t node);
    /** Passive service notification; never schedules or reserves compute. */
    void ObserveCompute(uint32_t node, bool busy);
    /** Check the event ledger against the preexisting cumulative service counters. */
    void VerifyHistory(uint32_t node) const;
    Ptr<ComputeService> Service(uint32_t node) const;
    Ptr<TaskCoordinator> m_tasks;
    Ptr<FaultModelEngine> m_faults;
    CheckpointManager& m_manager;
    int64_t m_stopNs;
    N5cVariant m_variant;
    bool m_spatialDiagnostics; ///< False collects resource observations only, never spatial proposals.
    N5cQuotaLedger m_quotas;
    ComputeUsageHistory m_computeHistory; ///< Actual normal/recovery service intervals only.
    std::map<uint32_t, Ptr<ComputeService>> m_services; ///< Observer lifetime owners.
    std::map<uint32_t, N5cNodeObservation> m_nodes;
    std::map<uint64_t, uint32_t> m_assignments; ///< Match idempotent shared-runtime release callbacks.
    std::map<uint64_t, int64_t> m_ready;
    std::vector<N5cDecisionTrace> m_decisions;
    bool m_finalized{};
};
} // namespace ns3::protection
#endif
