/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_BACKUP_STORAGE_POOL_H
#define SATCOMPUTE_BACKUP_STORAGE_POOL_H

#include <cstdint>
#include <map>
#include <optional>

namespace ns3::protection
{
/** Backup-only object roles; ordinary active task memory is not charged here. */
enum class StorageKind
{
    LOCAL_RECORD,
    REMOTE_STATE,
    REMOTE_BATCH,
    INIT_TEMP
};

/** A pool-scoped, non-recycled object identity and its accounting state. */
struct StorageEntry
{
    uint64_t taskId;  ///< Logical task owner.
    StorageKind kind; ///< Backup object role.
    uint64_t bytes;   ///< Exact application bytes, including applicable record metadata.
    bool reserved;    ///< Reserved capacity is not valid received state.
};

/** Exact per-node extra-storage ledger; never schedules events or evicts other tasks. */
class BackupStoragePool
{
  public:
    /** Construct with explicit capacity from platform parameters, in bytes. */
    explicit BackupStoragePool(uint64_t capacity);
    BackupStoragePool(const BackupStoragePool&) = delete;
    BackupStoragePool& operator=(const BackupStoragePool&) = delete;
    /** Reserve bytes for a task; null means capacity exhaustion, not task failure. */
    std::optional<uint64_t> TryReserve(uint64_t taskId, StorageKind kind, uint64_t bytes);
    /** Allocate already materialized bytes; zero-byte states still have an identity. */
    std::optional<uint64_t> Allocate(uint64_t taskId, StorageKind kind, uint64_t bytes);
    /** Convert an existing reservation to used bytes, without changing total capacity. */
    bool CommitReservation(uint64_t id);
    /** Release only an outstanding reservation; duplicate/stale release is harmless. */
    bool ReleaseReservation(uint64_t id);
    /** Release only a used entry; cannot accidentally consume an outstanding reservation. */
    bool Release(uint64_t id);
    /** Atomically merge same-owner used objects in place, retaining stateId.
     * New state must fit old state plus batch; no third full-state allocation.
     */
    bool Merge(uint64_t stateId, uint64_t batchId, uint64_t committedBytes);
    /** Release all used/reserved entries of a terminal task; return object count. */
    uint64_t ReleaseTask(uint64_t taskId);
    /** Read an entry without creating it. */
    const StorageEntry* Find(uint64_t id) const;

    /** Configured capacity in bytes. */
    uint64_t Capacity() const
    {
        return m_capacity;
    }

    /** Materialized bytes. */
    uint64_t Used() const
    {
        return m_used;
    }

    /** Promised but not yet materialized bytes. */
    uint64_t Reserved() const
    {
        return m_reserved;
    }

    /** Unpromised capacity. */
    uint64_t Free() const
    {
        return m_capacity - m_used - m_reserved;
    }

    /** Maximum materialized bytes observed. */
    uint64_t PeakUsed() const
    {
        return m_peakUsed;
    }

    /** Maximum reserved bytes observed. */
    uint64_t PeakReserved() const
    {
        return m_peakReserved;
    }

    /** Maximum simultaneous used plus reserved bytes. */
    uint64_t PeakTotal() const
    {
        return m_peakTotal;
    }

    /** Rejected capacity requests, not programmer errors. */
    uint64_t AllocationFailures() const
    {
        return m_failures;
    }

  private:
    /** Shared insertion with checked identity allocation. */
    std::optional<uint64_t> Insert(uint64_t taskId, StorageKind kind, uint64_t bytes, bool reserve);
    /** Refresh peaks after a successful mutation. */
    void UpdatePeaks();
    uint64_t m_capacity;                        ///< Configured byte limit.
    uint64_t m_used{};                          ///< Current used bytes.
    uint64_t m_reserved{};                      ///< Current reservations.
    uint64_t m_nextId{1};                       ///< Zero is invalid; identities never recycled.
    uint64_t m_peakUsed{};                      ///< Used high-water mark.
    uint64_t m_peakReserved{};                  ///< Reserved high-water mark.
    uint64_t m_peakTotal{};                     ///< Simultaneous high-water mark.
    uint64_t m_failures{};                      ///< Capacity failures.
    std::map<uint64_t, StorageEntry> m_entries; ///< Stable object ledger.
};
} // namespace ns3::protection
#endif
