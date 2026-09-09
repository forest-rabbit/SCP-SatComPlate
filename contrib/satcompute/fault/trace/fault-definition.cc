/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-definition.h"

#include "ns3/abort.h"

#include <limits>

namespace ns3
{

const char*
FaultTypeToString(FaultType type)
{
    switch (type)
    {
    case FaultType::COMPUTE:
        return "compute";
    case FaultType::SATELLITE:
        return "satellite";
    }
    return "unknown";
}

std::optional<int64_t>
FaultDefinition::GetRecoveryTimeNs() const
{
    if (!startTimeNs.has_value() || !durationNs.has_value())
    {
        return std::nullopt;
    }
    NS_ABORT_MSG_IF(startTimeNs.value() < 0 || durationNs.value() <= 0 ||
                        startTimeNs.value() >
                            std::numeric_limits<int64_t>::max() - durationNs.value(),
                    "validated fault has an invalid recovery time");
    return startTimeNs.value() + durationNs.value();
}

int64_t
FaultDefinition::GetAnchorTimeNs() const
{
    NS_ABORT_MSG_IF(!startTimeNs.has_value(),
                    "validated fault has no start time");
    return startTimeNs.value();
}

} // namespace ns3
