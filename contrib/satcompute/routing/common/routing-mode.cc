/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 定义 SatCompute 五种公开路由模式的唯一字符串映射。

#include "routing-mode.h"

#include "ns3/abort.h"

namespace ns3
{

bool
TryParseRoutingMode(const std::string& name, RoutingMode& mode)
{
    if (name == "global-first")
    {
        mode = RoutingMode::GLOBAL_FIRST;
        return true;
    }
    if (name == "global-hash-per-flow")
    {
        mode = RoutingMode::HASH_PER_FLOW;
        return true;
    }
    if (name == "global-hrw-per-flow")
    {
        mode = RoutingMode::HRW_PER_FLOW;
        return true;
    }
    if (name == "global-size-aware-hrw")
    {
        mode = RoutingMode::SIZE_AWARE_HRW;
        return true;
    }
    if (name == "global-capacity-aware-hrw")
    {
        mode = RoutingMode::CAPACITY_AWARE_HRW;
        return true;
    }
    return false;
}

const char*
GetRoutingModeName(RoutingMode mode)
{
    switch (mode)
    {
    case RoutingMode::GLOBAL_FIRST:
        return "global-first";
    case RoutingMode::HASH_PER_FLOW:
        return "global-hash-per-flow";
    case RoutingMode::HRW_PER_FLOW:
        return "global-hrw-per-flow";
    case RoutingMode::SIZE_AWARE_HRW:
        return "global-size-aware-hrw";
    case RoutingMode::CAPACITY_AWARE_HRW:
        return "global-capacity-aware-hrw";
    }
    NS_ABORT_MSG("未知 RoutingMode enum");
}

bool
IsReservationAwareRoutingMode(RoutingMode mode)
{
    return mode == RoutingMode::SIZE_AWARE_HRW || mode == RoutingMode::CAPACITY_AWARE_HRW;
}

} // namespace ns3
