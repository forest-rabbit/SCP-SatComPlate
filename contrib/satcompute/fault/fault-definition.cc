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
    if (!durationNs.has_value())
    {
        return std::nullopt;
    }
    NS_ABORT_MSG_IF(startTimeNs < 0 || durationNs.value() <= 0 ||
                        startTimeNs >
                            std::numeric_limits<int64_t>::max() - durationNs.value(),
                    "validated fault has an invalid recovery time");
    return startTimeNs + durationNs.value();
}

std::optional<int64_t>
FaultDefinition::GetWarningLeadTimeNs() const
{
    if (!noticeTimeNs.has_value())
    {
        return std::nullopt;
    }
    NS_ABORT_MSG_IF(noticeTimeNs.value() < 0 || noticeTimeNs.value() > startTimeNs,
                    "validated fault has an invalid notice time");
    return startTimeNs - noticeTimeNs.value();
}

} // namespace ns3
