/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup-storage-pool.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
BackupStoragePool::BackupStoragePool(uint64_t capacity) : m_capacity(capacity)
{
}

void
BackupStoragePool::UpdatePeaks()
{
    m_peakUsed = std::max(m_peakUsed, m_used);
    m_peakReserved = std::max(m_peakReserved, m_reserved);
    m_peakTotal = std::max(m_peakTotal, m_used + m_reserved);
    std::map<uint64_t, uint64_t> totals;
    for (const auto& [id, entry] : m_entries)
        totals[entry.taskId] += entry.bytes;
    for (const auto& [task, bytes] : totals)
        m_taskPeaks[task] = std::max(m_taskPeaks[task], bytes);
    if (m_peakObserver)
        m_peakObserver();
}

std::optional<uint64_t>
BackupStoragePool::Insert(uint64_t taskId, StorageKind kind, uint64_t bytes, bool reserve)
{
    if (!taskId)
        throw std::invalid_argument("backup storage requires a logical task ID");
    if (bytes > Free())
    {
        if (m_failures == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("storage failure counter overflow");
        ++m_failures;
        m_failedTasks.insert(taskId);
        return std::nullopt;
    }
    if (!m_nextId)
        throw std::overflow_error("storage object ID exhausted");
    const auto id = m_nextId;
    m_entries.emplace(id, StorageEntry{taskId, kind, bytes, reserve});
    m_nextId = id == std::numeric_limits<uint64_t>::max() ? 0 : id + 1;
    (reserve ? m_reserved : m_used) += bytes;
    UpdatePeaks();
    return id;
}

std::optional<uint64_t>
BackupStoragePool::TryReserve(uint64_t taskId, StorageKind kind, uint64_t bytes)
{
    return Insert(taskId, kind, bytes, true);
}

std::optional<uint64_t>
BackupStoragePool::Allocate(uint64_t taskId, StorageKind kind, uint64_t bytes)
{
    return Insert(taskId, kind, bytes, false);
}

const StorageEntry*
BackupStoragePool::Find(uint64_t id) const
{
    const auto it = m_entries.find(id);
    return it == m_entries.end() ? nullptr : &it->second;
}

bool
BackupStoragePool::CommitReservation(uint64_t id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end() || !it->second.reserved)
        return false;
    m_reserved -= it->second.bytes;
    m_used += it->second.bytes;
    it->second.reserved = false;
    UpdatePeaks();
    return true;
}

bool
BackupStoragePool::ReleaseReservation(uint64_t id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end() || !it->second.reserved)
        return false;
    m_reserved -= it->second.bytes;
    m_entries.erase(it);
    return true;
}

bool
BackupStoragePool::Release(uint64_t id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end() || it->second.reserved)
        return false;
    m_used -= it->second.bytes;
    m_entries.erase(it);
    return true;
}

bool
BackupStoragePool::Merge(uint64_t stateId, uint64_t batchId, uint64_t committedBytes)
{
    auto state = m_entries.find(stateId), batch = m_entries.find(batchId);
    if (stateId == batchId || state == m_entries.end() || batch == m_entries.end() ||
        state->second.reserved || batch->second.reserved ||
        state->second.taskId != batch->second.taskId ||
        state->second.kind != StorageKind::REMOTE_STATE ||
        (batch->second.kind != StorageKind::REMOTE_BATCH &&
         batch->second.kind != StorageKind::INIT_TEMP))
        return false;
    const auto before = state->second.bytes + batch->second.bytes;
    if (committedBytes > before)
        return false;
    m_used -= before - committedBytes;
    state->second.bytes = committedBytes;
    m_entries.erase(batch);
    return true;
}

uint64_t
BackupStoragePool::ReleaseTask(uint64_t taskId)
{
    return ReleaseTaskExcept(taskId, {});
}

uint64_t
BackupStoragePool::ReleaseTaskExcept(uint64_t taskId, const std::set<uint64_t>& retained)
{
    uint64_t count = 0;
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (it->second.taskId != taskId || retained.contains(it->first))
        {
            ++it;
            continue;
        }
        (it->second.reserved ? m_reserved : m_used) -= it->second.bytes;
        it = m_entries.erase(it);
        ++count;
    }
    return count;
}
} // namespace ns3::protection
