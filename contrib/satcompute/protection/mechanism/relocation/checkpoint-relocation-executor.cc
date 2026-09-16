/* SPDX-License-Identifier: GPL-2.0-only */
#include "checkpoint-relocation-executor.h"
namespace ns3::protection
{
RelocationReservationResult ReserveRelocationDestination(
    BackupStoragePool& pool, uint64_t taskId, uint64_t stateBytes,
    std::optional<uint64_t> tailBytes, uint64_t& stateObject, uint64_t& tailObject)
{
    const auto object = pool.TryReserve(taskId, StorageKind::REMOTE_STATE, stateBytes);
    if (!object) return RelocationReservationResult::STATE_UNAVAILABLE;
    stateObject = *object;
    if (tailBytes)
    {
        const auto tail = pool.TryReserve(taskId, StorageKind::REMOTE_BATCH, *tailBytes);
        if (!tail) return RelocationReservationResult::TAIL_UNAVAILABLE;
        tailObject = *tail;
    }
    return RelocationReservationResult::READY;
}
void DispatchCheckpointRelocation(uint32_t stateSource, uint32_t tailSource, uint32_t destination,
                                  uint64_t stateBytes, std::optional<uint64_t> tailBytes,
                                  uint64_t stateObject, uint64_t tailObject,
                                  const RelocationSend& send, const std::function<bool()>& live)
{
    send(ProtectionTransferKind::RECOVERY_STATE, stateSource, destination, stateBytes, stateObject);
    if (live() && tailBytes)
        send(ProtectionTransferKind::RECOVERY_TAIL, tailSource, destination, *tailBytes, tailObject);
}
} // namespace ns3::protection
