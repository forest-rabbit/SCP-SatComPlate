/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_TYPES_H
#define SATCOMPUTE_PROTECTION_TYPES_H
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ns3::protection
{
/** When the recovery node obtains original INPUT; state protection remains independent. */
enum class InputStagingPolicy
{
    EAGER,    ///< Preserve normal-period full INPUT staging and legacy committed state.
    DEFERRED, ///< Protect variable state only; fetch full INPUT after a fault.
    JIT       ///< State-only initialization and independent event-aware INPUT prefetch.
};

/** Shared layout choice, not a replacement for the three staging lifecycles. */
constexpr bool StateOnlyInitialization(InputStagingPolicy policy)
{
    return policy == InputStagingPolicy::DEFERRED || policy == InputStagingPolicy::JIT;
}

/** Mechanism-independent checkpoint, recovery or replica policy action. */
enum class ActionKind
{
    NONE,
    START_CHECKPOINT,
    UPDATE_CHECKPOINT,
    RECOMPUTE,
    START_REPLICA
};
/** Explicit logical protection phase, independent of storage byte count. */
enum class ProtectionPhase
{
    OFF,
    INITIALIZING,
    ON,
    RECOVERING,
    DONE
};
/** Recovery transfer/compute strategy, selected before any recovery execution. */
enum class RecoveryPath
{
    TAIL,
    REMOTE_REDO,
    RECOMPUTE
};
/** Additional application traffic; never mixed with ordinary INPUT/RESULT totals. */
enum class ProtectionTransferKind
{
    INIT_BASE,
    INIT_STATE,
    L1,
    REMOTE_BATCH,
    RECOVERY_TAIL,
    RECOVERY_INPUT,
    RECOVERY_RESULT, ///< Business output; shares canonical IDs, never protection byte totals.
    RECOVERY_STATE,  ///< Complete committed checkpoint relocated to a new recovery node.
    REPLICA_INPUT,
    REPLICA_RESULT,
    PREFETCH_INPUT ///< Independent full INPUT, retained across a fault when reusable.
};

/** Logical-task attempt role; replicas do not reuse primary identity. */
enum class AttemptRole
{
    PRIMARY,
    RECOVERY,
    REPLICA
};
/** Task execution stage distinct from protection setup state. */
enum class AttemptStage
{
    RUNNING,
    RECOVERING,
    RUNNING_BACKUP,
    RESULT,
    COMPLETED,
    FAILED
};

/** Stable pair identifying an attempt across compute/transfer callbacks. */
struct AttemptKey
{
    uint64_t taskId{};     ///< Logical task identity.
    uint64_t generation{}; ///< Primary zero, recovery one; never recycled.
    bool operator==(const AttemptKey&) const = default;
};

/** Fixed configuration chosen by policy; managers have no hidden defaults. */
struct CheckpointConfiguration
{
    uint32_t deltaPermille{}; ///< Increment interval, 50 means 5 percent.
    uint32_t batchN{};        ///< Number of contiguous L1 records per remote batch.
    uint32_t localNode{};     ///< Stable satellite ID.
    uint32_t remoteNode{};    ///< Stable recovery satellite ID.
};

/** Finite policy output; NONE requires no mechanism implementation. */
struct ProtectionAction
{
    ActionKind kind{ActionKind::NONE};                 ///< Selected action.
    std::optional<CheckpointConfiguration> checkpoint; ///< Present for checkpoint configuration.
    std::optional<uint32_t> replicaNode; ///< Present only for a true parallel-replica action.
};

/** Causal node candidate supplied by the runtime adapter, not a future trace. */
struct BackupCandidate
{
    uint32_t nodeId{}; ///< Stable satellite ID.
    bool healthy{};    ///< Current compute and satellite availability.
    bool idle{};       ///< No running task AND empty queue.
    bool reachable{};  ///< Current route exists; not a bandwidth guarantee.
    bool oneHop{};     ///< Eligible as local by the fixed placement rule.
    uint64_t queueDepth{}; ///< Causal queue length; idle remains the admission predicate.
    uint64_t storageFreeBytes{}; ///< Current free backup bytes, not a reservation.
    uint64_t backupAssignmentCount{}; ///< Active backup assignments, diagnostic input.
    uint64_t activeRecoveryCount{}; ///< Accepted nonterminal recovery ownership count.
};

/** Causal path feasibility supplied by runtime; never a capacity reservation. */
struct PlacementPathAvailability
{
    bool reachable{}, admissible{};
    std::string reason;
};
using PlacementPathPreview =
    std::function<PlacementPathAvailability(uint32_t, uint32_t)>;

/** Read-only event context for policy; no access to future faults or oracle results. */
struct ProtectionContext
{
    AttemptKey attempt;                          ///< Current attempt token.
    uint32_t primaryNode{};                      ///< Original compute node.
    int64_t nowNs{};                             ///< Observation timestamp.
    ProtectionPhase phase{ProtectionPhase::OFF}; ///< Runtime protection phase.
    bool firstComputeStart{};                    ///< True only for the first primary dispatch.
    bool taskSelected{}; ///< Explicit fixture/experiment filter, not a probability decision.
    std::vector<BackupCandidate> candidates; ///< Current candidate snapshots.
    PlacementPathPreview previewPath; ///< Borrowed synchronous decision snapshot, never retained.
    std::function<bool(uint32_t)> backupNodeFeasible; ///< Borrowed operation-specific single-node checks.
};

/** Already accepted attempt guard; does not modify any node health or fault RNG. */
class ExecutionAttempt
{
  public:
    /** Create a primary attempt using the already established logical-task deadline. */
    ExecutionAttempt(uint64_t taskId, uint32_t primaryNode, int64_t deadlineNs);
    /** Accept exactly one recovery after aggregate same-ns health/idle checks. */
    bool AcceptRecovery(uint32_t node, bool healthyAndIdle, int64_t nowNs);
    /** Mark real recovery service dispatch; blocked/stale attempts cannot dispatch. */
    bool StartRecovery(AttemptKey key, int64_t nowNs);
    /** Apply a compute outage or permanent satellite fault to this attempt only. */
    bool ApplyFault(bool permanentSatelliteFault);
    /** Authorize one real completion, checking the original deadline and attempt token. */
    bool CompleteCompute(AttemptKey key, int64_t nowNs);
    /** Finalize delivered result exactly once. */
    bool CompleteResult(AttemptKey key);
    /** Finalize any active attempt, e.g. timeout or failed transfer. */
    bool Fail();
    /** Only the currently owned attempt may change the logical task. */
    bool Owns(AttemptKey key) const;
    /** Whether subsequent F1/F2 termination is suppressed for this attempt. */
    bool ImmuneToComputeFault() const;

    /** Active token; terminal instances still retain identity for audit. */
    AttemptKey Key() const
    {
        return m_key;
    }

    /** Current stage. */
    AttemptStage Stage() const
    {
        return m_stage;
    }

    /** Active role. */
    AttemptRole Role() const
    {
        return m_role;
    }

    /** Original deadline; never reset on recovery. */
    int64_t DeadlineNs() const
    {
        return m_deadline;
    }

    /** Active compute node. */
    uint32_t Node() const
    {
        return m_node;
    }

  private:
    AttemptKey m_key;                            ///< Non-recycled callback identity.
    uint32_t m_node;                             ///< Actual execution node.
    int64_t m_deadline;                          ///< Logical-task deadline.
    int64_t m_acceptedNs{-1}; ///< Recovery acceptance time.
    int64_t m_computeStartedNs{}; ///< Earliest completion time for the active attempt.
    AttemptRole m_role{AttemptRole::PRIMARY};    ///< Active execution role.
    AttemptStage m_stage{AttemptStage::RUNNING}; ///< Execution stage.
};

/** Select one causal estimate; missing paths are unavailable, ties favor remote redo. */
RecoveryPath ChooseRecoveryPath(std::optional<int64_t> tailEstimateNs,
                                std::optional<int64_t> remoteRedoEstimateNs);
} // namespace ns3::protection
#endif
