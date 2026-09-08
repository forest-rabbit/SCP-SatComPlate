/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_TEST_FAULT_INJECTION_H
#define SATCOMPUTE_TEST_FAULT_INJECTION_H

#include "ns3/fault-controller.h"

namespace ns3
{
/** Test-only executor access. No file input or production scheduling mode.
 * Historical recoverable satellite cases exercise the common executor, not F3.
 */
struct FaultControllerTestAccess
{
    static void Schedule(Ptr<FaultController> controller,
                         const std::vector<FaultDefinition>& faults,
                         const std::vector<uint32_t>& satelliteIds,
                         int64_t durationNs)
    {
        controller->ConfigureGeneration(satelliteIds, durationNs);
        for (const auto& fault : faults)
        {
            if (fault.noticeTimeNs)
            {
                auto notice = fault;
                notice.faultOccurred = false;
                notice.startTimeNs.reset();
                notice.warningLeadTimeNs.reset();
                notice.riskDurationNs.reset();
                notice.durationNs.reset();
                controller->m_batches[*fault.noticeTimeNs].push_back(
                    {FaultEventType::NOTICE, notice});
            }
            if (!fault.faultOccurred)
            {
                const auto clear = fault.GetRiskClearTimeNs();
                if (clear && *clear < durationNs)
                {
                    controller->m_batches[*clear].push_back({FaultEventType::NOTICE_CLEAR, fault});
                }
            }
            else
            {
                controller->m_batches[fault.startTimeNs.value()].push_back(
                    {FaultEventType::START, fault});
                controller->ScheduleRecovery(fault);
            }
        }
        for (const auto& [timeNs, events] : controller->m_batches)
        {
            static_cast<void>(events);
            controller->ScheduleBatch(timeNs);
        }
    }
};
} // namespace ns3
#endif
