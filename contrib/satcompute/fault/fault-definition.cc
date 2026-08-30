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

std::optional<int64_t>
FaultDefinition::GetWarningLeadTimeNs() const
{
    if (!noticeTimeNs.has_value() || !startTimeNs.has_value())
    {
        return std::nullopt;
    }
    NS_ABORT_MSG_IF(noticeTimeNs.value() < 0 ||
                        noticeTimeNs.value() > startTimeNs.value(),
                    "validated fault has an invalid notice time");
    return startTimeNs.value() - noticeTimeNs.value();
}

std::optional<int64_t>
FaultDefinition::GetRiskClearTimeNs() const
{
    if (!noticeTimeNs.has_value() || !riskDurationNs.has_value())
    {
        return std::nullopt;
    }
    NS_ABORT_MSG_IF(noticeTimeNs.value() < 0 || riskDurationNs.value() <= 0 ||
                        noticeTimeNs.value() >
                            std::numeric_limits<int64_t>::max() - riskDurationNs.value(),
                    "validated fault has an invalid risk clear time");
    return noticeTimeNs.value() + riskDurationNs.value();
}

int64_t
FaultDefinition::GetAnchorTimeNs() const
{
    if (noticeTimeNs.has_value())
    {
        return noticeTimeNs.value();
    }
    NS_ABORT_MSG_IF(!startTimeNs.has_value(),
                    "validated fault has neither notice nor start time");
    return startTimeNs.value();
}

} // namespace ns3
