/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ROUTING_MODE_H
#define SATCOMPUTE_ROUTING_MODE_H

#include <string>

namespace ns3
{

enum class RoutingMode
{
    GLOBAL_FIRST,
    HASH_PER_FLOW,
    HRW_PER_FLOW,
    SIZE_AWARE_HRW,
    CAPACITY_AWARE_HRW
};

bool TryParseRoutingMode(const std::string& name, RoutingMode& mode);
const char* GetRoutingModeName(RoutingMode mode);
bool IsReservationAwareRoutingMode(RoutingMode mode);

} // namespace ns3

#endif
