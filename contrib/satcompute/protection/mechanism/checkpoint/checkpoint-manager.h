/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CHECKPOINT_MANAGER_H
#define SATCOMPUTE_CHECKPOINT_MANAGER_H
#include "../../../task/task-coordinator.h"
#include "../../runtime/protection-runtime.h"
#include "../../runtime/protection-transfer-key.h"
#include "../../storage/backup-storage-pool.h"
#include "checkpoint-progress.h"
#include <functional>
#include <memory>
#include <string>

namespace ns3::protection
{
/** Immutable fault-time state; identifiers refer to retained physical objects, not predictions. */
struct RecoverySnapshot
{
    uint64_t taskId{}, actualWork{}, localWork{}, remoteWork{}, remoteObject{}, remoteBytes{},
        tailBytes{}; ///< Fault-time WU and exact backing bytes including record H.
    uint32_t localNode{}, remoteNode{}; ///< Frozen checkpoint placement.
    std::string phase{"OFF"};           ///< OFF, INITIALIZING or ON before quiescence.
    int64_t faultNs{}, deadlineNs{}, localCostNs{},
        remoteCostNs{};                        ///< Original causal times/costs.
    std::map<uint64_t, uint64_t> localObjects; ///< Covered WU -> retained used pool identity.
    uint64_t pendingRecords{}, inFlightFlows{},
        pendingRemoteObject{}; ///< Discarded non-valid work.
    std::vector<uint64_t> pendingLocalWorks, inFlightLocalTransfers,
        inFlightRemoteTransfers; ///< Exact pending boundaries and real in-flight identities.
    bool remoteMergePending{};   ///< Received but not fault-usable cR/commit operation.
};
/** Exact transition evidence, separate from ordinary task/transfer statistics. */
struct ProtectionEvent
{
    uint64_t taskId{}, generation{};    ///< Logical task and owning attempt.
    int64_t timeNs{};                   ///< Simulator timestamp.
    std::string event;                  ///< Stable event name, not free-form CSV text.
    uint32_t localNode{}, remoteNode{}; ///< Selected satellite identities.
    uint64_t work{}, bytes{}, localWork{}, remoteWork{}, actualWork{}; ///< Event payload and l/r/x.
    uint64_t localUsed{}, localReserved{}, remoteUsed{},
        remoteReserved{};   ///< Whole-pool snapshots.
    uint32_t storageNode{}; ///< Object owner pool; meaningful when storageObject is nonzero.
    uint64_t storageObject{}, transferId{}; ///< Exact object/flow IDs; zero when not applicable.
};

/** One registered real flow; storage ownership exists before this record is created. */
struct ProtectionFlow
{
    ProtectionTransferKey key;                               ///< Stable logical request identity.
    uint64_t transferId{}, bytes{}, work{}, storageObject{}; ///< Real flow and reserved payload.
    uint32_t storageNode{};                                  ///< Destination pool identity.
    int64_t requestedNs{}; ///< Creation time before canonical registration.
};

/** Per-attempt checkpoint evidence, independent of task success or recovery claims. */
struct ProtectionTaskSummary
{
    uint64_t taskId{}, inputBytes{}, work{}, variableBytes{}; ///< Immutable task budget.
    uint32_t primaryNode{}, localNode{}, remoteNode{}, deltaPermille{}, batchN{}; ///< Fixed action.
    int64_t startNs{-1}, initializationNs{-1}, stopNs{-1}, localCostNs{},
        remoteCostNs{}; ///< Timing.
    uint64_t localWork{}, remoteWork{}, generated{}, localCommits{},
        remoteCommits{};    ///< Evidence counts.
    std::string stopReason; ///< First protection-stop reason.
};

/** Real checkpoint data path and retained fault snapshots; never mutates primary compute. */
class CheckpointManager : public ProtectionMechanism
{
  public:
    /** Bind existing services; validate typed tasks before any simulator events.
     * @param tasks Existing task coordinator, retained for the manager lifetime.
     * @param topology Existing satellite runtime (no second network).
     * @param storageBytesPerNode Explicit per-node capacity; zero deliberately rejects all bases.
     * @param simulationStopNs Absolute simulation endpoint in ns.
     */
    CheckpointManager(Ptr<TaskCoordinator> tasks,
                      SatelliteRuntimeView& topology,
                      uint64_t storageBytesPerNode,
                      int64_t simulationStopNs);
    ~CheckpointManager() override;
    bool Supports(ActionKind kind) const override;
    void Execute(const ProtectionContext& context, const ProtectionAction& action) override;
    bool OnComputeFault(const ProtectionContext&) override;
    void OnTaskComputeComplete(AttemptKey attempt) override;
    void OnTaskTerminal(uint64_t taskId) override;
    /** Idempotently stop all remaining protection after Simulator::Run. */
    void Finalize();

    /** Enable strict fault-time object retention; no-fault G2 timing stays unchanged. */
    void EnableRecoveryRetention()
    {
        m_recoveryRetention = true;
    }

    /** Freeze before stopping any primary/protection operation. */
    RecoverySnapshot FreezeRecoverySnapshot(uint64_t taskId, int64_t faultNs);
    /** Quiesce generation/flows while retaining only snapshot backing objects. */
    void QuiesceForRecovery(const RecoverySnapshot& snapshot);
    /** Explicit recovery ownership handoff/terminal cleanup; never evicts other tasks. */
    void ReleaseRecoveryState(uint64_t taskId);
    /** Append a G3 event with its frozen progress and current pool accounting. */
    void RecordRecoveryEvent(const RecoverySnapshot& snapshot,
                             const std::string& event,
                             uint64_t bytes,
                             uint64_t transferId);

    /** Shared pool for recovery temporary reservations and in-place merges. */
    BackupStoragePool& Pool(uint32_t node)
    {
        return *m_pools.at(node);
    }

    /** Queue a cross-node recovery flow through the same canonical ID allocator. */
    void QueueRecovery(ProtectionTransferKey key,
                       uint32_t source,
                       uint32_t destination,
                       uint64_t bytes,
                       uint64_t work,
                       uint64_t object,
                       std::function<bool()> live,
                       std::function<void(uint64_t)> registered);

    /** @return Causal event history including storage identities and snapshots. */
    const std::vector<ProtectionEvent>& Events() const
    {
        return m_events;
    }

    /** @return All registered real flows, including failed/cancelled history. */
    const std::vector<ProtectionFlow>& Flows() const
    {
        return m_flows;
    }

    /** @return Stable task-ID ordered summaries. */
    std::vector<ProtectionTaskSummary> Summaries() const;

    /** @return Node-ID ordered extra-storage ledgers. */
    const std::map<uint32_t, std::unique_ptr<BackupStoragePool>>& Pools() const
    {
        return m_pools;
    }

  private:
    /** One immutable captured increment, including an explicit missing-receipt gap. */
    struct Record
    {
        uint64_t from{}, work{}, bytes{},
            object{};    ///< Captured endpoints, byte budget and pool ID.
        bool received{}; ///< True only after receiver-complete.
        int64_t receivedNs{-1}; ///< Strict validity boundary for same-ns faults.
    };

    /** One primary checkpoint lifecycle; address remains stable until manager destruction. */
    struct State
    {
        State(const TaskRuntime& task,
              CheckpointConfiguration config,
              uint64_t initial,
              int64_t now,
              uint64_t rate);
        const TaskRuntime& task; ///< Read-only ordinary runtime; coordinator outlives manager.
        CheckpointConfiguration config; ///< Immutable fixed action.
        TaskStateAdapter layout;        ///< Exact production layout, no shadow dependency.
        CheckpointProgress progress;    ///< Valid contiguous progress and internal merge timing.
        ProtectionTaskSummary summary;  ///< Historical evidence retained after stop.
        uint64_t rate{}, initial{}, triggered{}, baseObject{}, initObject{},
            batchObject{};    ///< WU/s and owned IDs.
        uint64_t batchWork{}; ///< Immutable in-flight batch target.
        int64_t baseReceivedNs{-1},
            stateReceivedNs{-1}; ///< Initialization path completion evidence.
        bool active{true}, batchInFlight{}, batchBlocked{}; ///< Terminal and no-retry guards.
        std::map<uint64_t, Record> records; ///< Captured records ordered by completed WU.
        std::vector<EventId> timers;        ///< Task-scoped cancellable generation/merge events.
        std::optional<std::pair<int64_t, bool>> physicalCommit; ///< Nominal time and init flag.
    };

    /** Reserved positive-byte request awaiting canonical next-ns registration. */
    struct Request
    {
        ProtectionTransferKey key;          ///< Stable request key.
        uint32_t source{}, destination{};   ///< Actual satellite endpoints.
        uint64_t bytes{}, work{}, object{}; ///< Reserved immutable payload.
        int64_t requestedNs{};              ///< Original creation ns, not event UID.
        std::function<bool()> live; ///< Recovery attempt guard; empty for primary protection.
        std::function<void(uint64_t)> registered; ///< Install terminal observer before start.
    };

    /** Check owning primary service; inclusive compute end takes precedence over callbacks. */
    bool Live(State& state);
    /** @return Causal completed WU, bounded by total WU. */
    uint64_t Actual(const State& state) const;
    /** Schedule a guarded task-scoped callback strictly before simulation stop. */
    void Later(State& state, int64_t at, std::function<void()> callback);
    /** Append an invariant-checked event with current whole-pool snapshots. */
    void Log(State& state,
             const std::string& event,
             uint64_t work = 0,
             uint64_t bytes = 0,
             uint32_t storageNode = 0,
             uint64_t object = 0,
             uint64_t transfer = 0);
    /** Reserve first; log an explicit capacity failure without changing the ordinary task. */
    std::optional<uint64_t> Reserve(
        State& state, uint32_t node, StorageKind kind, uint64_t bytes, uint64_t work);
    /** Queue only an already reserved positive-byte object. */
    void Queue(State& state,
               ProtectionTransferKind kind,
               uint64_t sequence,
               uint32_t source,
               uint32_t destination,
               uint64_t bytes,
               uint64_t work,
               uint64_t object);
    void Flush(int64_t requestedNs); ///< Register/start sorted requests, rechecking live state.
    void TransferTerminal(uint64_t id, int64_t at); ///< Real finalizer callback, not sender finish.
    void InitializationReceived(State& state);      ///< Join both init paths and schedule cR.
    void ScheduleCapture(State& state);             ///< Select the next legal application boundary.
    void Capture(State& state, uint64_t work);      ///< Freeze bytes and schedule cL completion.
    void TryBatch(State& state); ///< Reserve/send exactly n contiguous received records.
    void Commit(State& state, bool initialization); ///< Atomic storage/progress commit and cleanup.
    void FinishPhysicalCommit(State& state, bool initialization); ///< Deferred same-ns retention.
    void Stop(State& state,
              const std::string& reason); ///< Cancel and release all task-owned state.
    Ptr<TaskCoordinator> m_tasks;         ///< Existing ordinary runtime owner.
    Ptr<NetworkTransferEngine> m_network; ///< Shared real traffic engine.
    int64_t m_stopNs;                     ///< Absolute simulation endpoint.
    ProtectionTransferIds m_ids;          ///< Disjoint non-recycled flow ID allocator.
    std::map<uint64_t, std::unique_ptr<State>> m_states;            ///< Stable task ownership.
    std::map<uint32_t, std::unique_ptr<BackupStoragePool>> m_pools; ///< Shared per-node capacity.
    std::map<int64_t, std::map<ProtectionTransferKey, Request>>
        m_requests;                           ///< Canonical request buckets.
    std::map<int64_t, EventId> m_flushEvents; ///< One registration event per request timestamp.
    std::map<uint64_t, size_t> m_flowIndexes; ///< Terminal callback lookup.
    std::vector<ProtectionEvent> m_events;    ///< Append-only causal evidence.
    std::vector<ProtectionFlow> m_flows;      ///< Append-only real flow metadata.
    bool m_recoveryRetention{};               ///< Explicit G3 fault-enabled phase ordering.
};
} // namespace ns3::protection
#endif
