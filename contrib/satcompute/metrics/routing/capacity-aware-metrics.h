/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CAPACITY_AWARE_METRICS_H
#define SATCOMPUTE_CAPACITY_AWARE_METRICS_H

#include "../../routing/state/capacity-reservation-state.h"

#include <string>

namespace ns3
{

void WriteCapacityAwareMetrics(const CapacityAwareRuntimeSummary& summary,
                               const std::string& outputDirectory);
void RemoveCapacityAwareMetrics(const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_CAPACITY_AWARE_METRICS_H
