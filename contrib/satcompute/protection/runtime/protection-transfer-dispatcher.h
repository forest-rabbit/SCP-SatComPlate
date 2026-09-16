/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_TRANSFER_DISPATCHER_H
#define SATCOMPUTE_PROTECTION_TRANSFER_DISPATCHER_H
#include "../../task/task-coordinator.h"
#include "protection-transfer-key.h"
#include <functional>
#include <map>
#include <vector>

namespace ns3::protection
{
/** One real registered flow; this ledger never owns checkpoint/storage objects. */
struct ProtectionFlow
{
    ProtectionTransferKey key; ///< Stable logical flow identity.
    uint64_t transferId{}, bytes{}, work{}, storageObject{}; ///< Exact registered payload metadata.
    uint32_t storageNode{}; ///< Destination pool identity; dispatcher never mutates it.
    int64_t requestedNs{}; ///< Original causal request time, not event UID.
};

/** Immutable payload queued for canonical next-ns registration. */
struct ProtectionTransferRequest
{
    ProtectionTransferKey key; ///< Canonical within-nanosecond ordering key.
    uint32_t source{}, destination{}; ///< Existing physical satellite endpoints.
    uint64_t bytes{}, work{}, object{}; ///< Immutable reserved payload and backing identity.
    int64_t requestedNs{}; ///< First creation time, retained across unregistered resource holds.
    std::function<bool()> live; ///< Recovery/replica guard; absent for checkpoint callback.
    std::function<void(uint64_t)> registered; ///< Installed before actual network start.
};

/** Shared queue/ID/evidence owner; no placement, checkpoint or recovery policy. */
class ProtectionTransferDispatcher
{
  public:
    using Request = ProtectionTransferRequest; ///< Neutral immutable request contract.
    /** Bind the existing network and optional checkpoint-specific registration callback. */
    explicit ProtectionTransferDispatcher(Ptr<TaskCoordinator> tasks,
                                          std::function<void(const Request&)> primary = {});
    ~ProtectionTransferDispatcher();
    /** Queue into the original request-time bucket, with one next-ns event per bucket. */
    void Queue(int64_t time, Request request, const char* duplicateMessage);
    /** Remove pending requests only; object cleanup remains with its mechanism owner. */
    void Discard(uint64_t taskId, bool primaryOnly = false);
    /** Cancel pending registrations without deleting append-only evidence. */
    void Finalize();
    /** True when every request bucket is empty. */
    bool Empty() const;
    /** Preserve the one shared, non-recycled ID stream. */
    uint64_t NextId() { return m_ids.Next(); }
    /** Append in actual registration order; return the stable index. */
    size_t Record(ProtectionFlow flow);
    /** Read-only evidence for mechanism callbacks and metrics. */
    const std::vector<ProtectionFlow>& Flows() const { return m_flows; }

  private:
    void Flush(int64_t time); ///< Extract before callbacks; sorted keys preserve same-ns order.
    void RegisterGuarded(const Request& request, int64_t time);
    Ptr<NetworkTransferEngine> m_network; ///< Sole actual network engine.
    ProtectionTransferIds m_ids; ///< One disjoint, non-recycled allocation stream.
    std::function<void(const Request&)> m_primary; ///< Mechanism-owned storage/maintenance checks.
    std::map<int64_t, std::map<ProtectionTransferKey, Request>> m_requests; ///< Causal buckets.
    std::map<int64_t, EventId> m_flushEvents; ///< One next-ns callback per bucket.
    std::vector<ProtectionFlow> m_flows; ///< Append-only real-flow evidence.
};
} // namespace ns3::protection
#endif
