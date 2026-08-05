/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SIZE_AWARE_METRICS_H
#define SATCOMPUTE_SIZE_AWARE_METRICS_H

#include "../../routing/state/flow-route-registry.h"

#include "ns3/ptr.h"

#include <string>

namespace ns3
{

void WriteSizeAwareMetrics(Ptr<FlowRouteRegistry> registry,
                           const std::string& outputDirectory);
void RemoveSizeAwareMetrics(const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_SIZE_AWARE_METRICS_H
