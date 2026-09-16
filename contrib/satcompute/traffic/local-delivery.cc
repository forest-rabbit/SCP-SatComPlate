/* SPDX-License-Identifier: GPL-2.0-only */
#include "local-delivery.h"
#include "ns3/simulator.h"
#include <stdexcept>

namespace ns3
{
const char*
DeliveryModeToString(DeliveryMode mode)
{
    return mode == DeliveryMode::LOCAL ? "LOCAL" : "NETWORK";
}

EventId
LocalDelivery::Schedule(uint32_t source,
                        uint32_t destination,
                        uint64_t bytes,
                        std::function<bool()> eligible,
                        std::function<void(uint64_t, int64_t)> complete)
{
    if (source != destination || !bytes || !eligible || !complete)
        throw std::invalid_argument(
            "LocalDelivery requires same-node positive-byte data and guards");
    return Simulator::Schedule(
        NanoSeconds(1), [bytes, eligible = std::move(eligible), complete = std::move(complete)] {
            if (eligible())
                complete(bytes, Simulator::Now().GetNanoSeconds());
        });
}
} // namespace ns3
