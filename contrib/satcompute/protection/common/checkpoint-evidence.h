/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CHECKPOINT_EVIDENCE_H
#define SATCOMPUTE_CHECKPOINT_EVIDENCE_H
#include "protection-types.h"
#include <map>
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
    uint64_t primaryRate{}, initGenerated{}, initCommitted{}, localGeneratedCostCount{},
        remoteCommittedCostCount{}, normalCostNs{}, localPeakBytes{}, remotePeakBytes{};
    ///< Real completed cost events; initialization excluded from recurrent counters.
};

} // namespace ns3::protection
#endif
