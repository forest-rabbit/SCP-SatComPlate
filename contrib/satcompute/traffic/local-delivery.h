/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_LOCAL_DELIVERY_H
#define SATCOMPUTE_LOCAL_DELIVERY_H
#include "ns3/event-id.h"
#include <cstdint>
#include <functional>

namespace ns3
{
/** Logical data movement, not a synthetic network transfer or packet. */
enum class DeliveryMode
{
    LOCAL,
    NETWORK
};
const char* DeliveryModeToString(DeliveryMode mode);

/** One same-satellite positive-byte delivery guarded at execution time. */
class LocalDelivery
{
  public:
    /** Schedule causal local readiness without UDP or network byte accounting.
     * @param source Original data holder.
     * @param destination Same satellite as source; otherwise rejected.
     * @param bytes Actual logical payload bytes, retained in completion evidence.
     * @param eligible Recheck attempt/health/deadline before delivery.
     * @param complete Called once with actual bytes and delivery timestamp.
     * @return Cancellable event. A 1ns phase boundary lets same-tick faults settle first.
     */
    static EventId Schedule(uint32_t source,
                            uint32_t destination,
                            uint64_t bytes,
                            std::function<bool()> eligible,
                            std::function<void(uint64_t, int64_t)> complete);
};
} // namespace ns3
#endif
