/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CHECKPOINT_RELOCATION_EXECUTOR_H
#define SATCOMPUTE_CHECKPOINT_RELOCATION_EXECUTOR_H
#include "../../storage/backup-storage-pool.h"
#include "../../common/protection-types.h"
#include <functional>
#include <optional>
namespace ns3::protection
{
/** Optional reusable mechanism, not a CompFRR-P scorer or permission to use tail. */
enum class RelocationReservationResult { READY, STATE_UNAVAILABLE, TAIL_UNAVAILABLE };
/** Reserve in original order. On failure retain already reserved identities for owner cleanup. */
RelocationReservationResult ReserveRelocationDestination(
    BackupStoragePool& pool, uint64_t taskId, uint64_t stateBytes,
    std::optional<uint64_t> tailBytes, uint64_t& stateObject, uint64_t& tailObject);
using RelocationSend = std::function<void(ProtectionTransferKind, uint32_t, uint32_t, uint64_t, uint64_t)>;
/** Same-epoch state then optional tail; actual INPUT/state concurrency is owned by runtime.
 * The caller supplies tail only when its scheme contract permits it.
 * The send callback keeps LocalDelivery and real network behavior; no artificial link.
 */
void DispatchCheckpointRelocation(uint32_t stateSource, uint32_t tailSource, uint32_t destination,
                                  uint64_t stateBytes, std::optional<uint64_t> tailBytes,
                                  uint64_t stateObject, uint64_t tailObject,
                                  const RelocationSend& send, const std::function<bool()>& live);
} // namespace ns3::protection
#endif
