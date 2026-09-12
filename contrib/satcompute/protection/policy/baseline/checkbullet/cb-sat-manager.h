/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CB_SAT_MANAGER_H
#define SATCOMPUTE_CB_SAT_MANAGER_H
#include "cb-sat-policy.h"
#include "cb-sat-state.h"
#include "../../../runtime/placement-load-ledger.h"
#include "../../../runtime/protection-transfer-key.h"
#include "../../../policy/placement-policy.h"
#include "../../../storage/backup-storage-pool.h"
#include "../../../../task/task-coordinator.h"
#include <memory>
#include <tuple>

namespace ns3::protection::checkbullet
{
/** CB semantic tags are independent of the double-tier mechanism's transfer enum. */
enum class CbFlowKind
{
    INIT_INPUT, INIT_FULL, DELTA, RELOCATE_INPUT, RELOCATE_FULL, RELOCATE_LOG,
    FALLBACK_INPUT, RESULT
};
const char* CbFlowName(CbFlowKind kind);

/** Causal object operation and physical pool accounting. */
struct CbEvent
{
    uint64_t task{}, sequence{}, work{}, bytes{}, object{}, transfer{}, root{}, recoverable{};
    int64_t timeNs{};
    uint32_t node{};
    uint64_t used{}, reserved{};
    std::string event, role;
};

/** One real network request, including failed and cancelled transmissions. */
struct CbFlow
{
    uint64_t task{}, generation{}, sequence{}, transfer{}, bytes{}, object{};
    uint32_t source{}, destination{};
    CbFlowKind kind{};
    int64_t requestedNs{}, registeredNs{-1}, terminalNs{-1};
    bool completed{};
};

/** Normal-period decision; selected node is a single role, never a fabricated pair. */
struct CbDecision
{
    uint64_t task{}, decision{}, work{}, quota{}, occupied{};
    int64_t timeNs{};
    std::optional<uint32_t> backup;
    CbThreshold threshold;
    std::string reason;
};

/** Attempt-local evidence remains available after cleanup. */
struct CbTaskSummary
{
    uint64_t task{}, inputBytes{}, work{}, variableBytes{}, rate{};
    uint32_t primary{};
    std::optional<uint32_t> backup;
    CbInterval interval;
    int64_t startNs{}, initializedNs{-1}, stoppedNs{-1}, localCostNs{}, remoteCostNs{};
    uint64_t generated{}, initialCommits{}, merges{}, normalCostNs{};
    uint64_t rootWork{}, recoverableWork{};
    std::string stopReason;
};

/** Frozen CB-owned objects; no lookup of another mechanism's task progress. */
struct CbRecoverySnapshot
{
    CbSnapshot state;
    uint64_t actualWork{}, inputObject{}, rootObject{};
    int64_t cutoffNs{}, deadlineNs{};
    std::map<uint64_t, uint64_t> logObjects; ///< Sequence -> retained physical identity on B.
};

/** Single-backup normal data path, using the platform's actual network and storage. */
class CbSatManager
{
  public:
    /** Bind one run; MTBF is explicit, not a production test-fixture default. */
    CbSatManager(Ptr<TaskCoordinator> tasks, SatelliteRuntimeView& topology,
                 uint64_t capacity, int64_t stopNs, double mtbfSeconds,
                 PlacementPolicy& placement, PlacementLoadLedger& loads);
    ~CbSatManager();
    void Finalize(); ///< Stop generation, real flows and every owned reservation.
    bool IsQuiescent() const; ///< No surviving physical CB state or live normal attempt.
    CbRecoverySnapshot Freeze(uint64_t task, int64_t faultNs);
    void RetainForRecovery(const CbRecoverySnapshot& snapshot);
    void ReleaseTask(uint64_t task, const std::string& reason);
    void InvalidateNode(uint32_t node); ///< F3 removes only physical objects on the damaged node.
    /** Apply exactly the approved frozen chain at its declared current holder; never discover state. */
    bool ApplyStoredLogs(const CbRecoverySnapshot& snapshot, uint32_t holder,
                         uint64_t root, const std::map<uint64_t, uint64_t>& logs);
    std::optional<CbSnapshot> Snapshot(uint64_t task) const;
    const CbState* State(uint64_t task) const;
    std::vector<CbTaskSummary> Summaries() const;
    const std::vector<CbEvent>& Events() const { return m_events; }
    const std::vector<CbFlow>& Flows() const { return m_flows; }
    const std::vector<CbDecision>& Decisions() const { return m_decisions; }
    const auto& Pools() const { return m_pools; }
    uint64_t GlobalStoragePeakBytes() const { return m_globalPeak; }

    /** Explicit storage ownership API, also used by CB recovery temporary objects. */
    std::optional<uint64_t> Reserve(uint64_t task, uint32_t node,
                                     const std::string& role, uint64_t bytes);
    void CommitObject(uint32_t node, uint64_t object);
    void ReleaseObject(uint32_t node, uint64_t object, const std::string& reason);
    /** Actual ownership-floor share, with optional zero-occupancy candidate included. */
    uint64_t Quota(uint32_t node, uint64_t task) const;
    uint64_t Occupied(uint32_t node, uint64_t task) const;
    /** Canonical next-ns registration; callback sees real receipt/failure, not sender finish. */
    void Queue(uint64_t task, uint64_t generation, CbFlowKind kind, uint64_t sequence,
               uint32_t source, uint32_t destination, uint64_t bytes, uint64_t object,
               std::function<bool()> live, std::function<void(uint64_t, bool)> terminal);
    void CancelFlows(uint64_t task, uint64_t generation);
    void Log(uint64_t task, const std::string& event, uint32_t node = 0,
             uint64_t object = 0, uint64_t sequence = 0, uint64_t work = 0,
             uint64_t bytes = 0, uint64_t transfer = 0, const std::string& role = "");

  private:
    struct RecordObjects
    {
        uint64_t source{}, destination{};
        bool generated{}, sending{};
    };
    struct Normal
    {
        Normal(const TaskRuntime& task, uint64_t rate, double mtbf);
        const TaskRuntime& task;
        TaskStateAdapter layout;
        CbTaskSummary summary;
        std::unique_ptr<CbState> state;
        size_t nextTarget{};
        uint64_t inputObject{}, rootObject{}, decision{}, mergeSequence{};
        bool live{true}, retained{}, inputSending{}, initializationScheduled{}, merging{};
        std::optional<int64_t> physicalCommit;
        std::vector<uint64_t> mergeLogs;
        std::map<uint64_t, RecordObjects> objects;
        std::set<uint64_t> retryAtBoundary; ///< Failed receipts never trigger an off-cadence retry.
        std::vector<EventId> timers;
    };
    struct Request
    {
        CbFlow flow;
        std::function<bool()> live;
        std::function<void(uint64_t, bool)> terminal;
    };
    using RequestKey = std::tuple<uint64_t, uint64_t, CbFlowKind, uint64_t>;
    void OnTask(const TaskEventRecord& event);
    bool Live(const Normal& normal) const;
    uint64_t Actual(const Normal& normal) const;
    void Later(Normal& normal, int64_t at, std::function<void()> callback);
    void ScheduleTarget(Normal& normal);
    void Boundary(Normal& normal, CbTarget target);
    bool Select(Normal& normal, CbTarget target);
    CbThreshold Threshold(const Normal& normal, uint32_t node, uint64_t initial = 0) const;
    void SendPending(Normal& normal);
    void Received(Normal& normal, uint64_t sequence, uint64_t transfer, bool completed);
    void TryMerge(Normal& normal);
    void FinishPhysicalCommit(Normal& normal);
    void Stop(Normal& normal, const std::string& reason, bool retain = false);
    void Changed(uint32_t node); ///< Re-evaluate only affected owners' future merge decisions.
    void Flush(int64_t requestedNs);
    void TransferTerminal(uint64_t transfer, int64_t at);
    Ptr<ComputeService> Service(uint32_t node) const;
    Ptr<TaskCoordinator> m_tasks;
    SatelliteRuntimeView& m_topology;
    Ptr<NetworkTransferEngine> m_network;
    int64_t m_stopNs;
    double m_mtbf;
    PlacementPolicy& m_placement;
    PlacementLoadLedger& m_loads;
    ProtectionTransferIds m_ids;
    uint64_t m_globalPeak{};
    std::map<uint64_t, std::unique_ptr<Normal>> m_normal;
    std::map<uint32_t, std::unique_ptr<BackupStoragePool>> m_pools;
    std::map<uint32_t, std::map<uint64_t, std::string>> m_roles;
    std::map<int64_t, std::map<RequestKey, Request>> m_requests;
    std::map<int64_t, EventId> m_flushes;
    std::map<uint32_t, EventId> m_changed;
    std::map<uint64_t, size_t> m_flowIndexes;
    std::map<uint64_t, std::function<void(uint64_t, bool)>> m_terminals;
    std::vector<CbEvent> m_events;
    std::vector<CbFlow> m_flows;
    std::vector<CbDecision> m_decisions;
};
} // namespace ns3::protection::checkbullet
#endif
