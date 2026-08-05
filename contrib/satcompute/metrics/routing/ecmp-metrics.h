/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ECMP_METRICS_H
#define SATCOMPUTE_ECMP_METRICS_H

#include "../../routing/ns3/satcompute-ipv4-global-routing.h"

#include <string>
#include <vector>

namespace ns3
{

void WriteEcmpRouteEvents(const std::vector<EcmpRouteDecisionEvent>& routeEvents,
                          const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_ECMP_METRICS_H
