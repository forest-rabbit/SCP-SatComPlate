/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 为下一跳级和完整路径级路由模式创建独立策略实例。

#include "routing-policy-factory.h"

#include "algorithm/capacity-aware-hrw-policy.h"
#include "algorithm/global-first-policy.h"
#include "algorithm/hash-per-flow-policy.h"
#include "algorithm/hrw-per-flow-policy.h"
#include "algorithm/size-aware-hrw-policy.h"

#include "ns3/abort.h"

namespace ns3
{

std::unique_ptr<NextHopPolicy>
RoutingPolicyFactory::CreateNextHopPolicy(RoutingMode mode,
                                          FlowRouteState* flowState,
                                          const SizeAwareLoadView* sizeAwareLoadView)
{
    switch (mode)
    {
    case RoutingMode::GLOBAL_FIRST:
        return std::make_unique<GlobalFirstPolicy>();
    case RoutingMode::HASH_PER_FLOW:
        return std::make_unique<HashPerFlowPolicy>();
    case RoutingMode::HRW_PER_FLOW:
        return std::make_unique<HrwPerFlowPolicy>();
    case RoutingMode::SIZE_AWARE_HRW:
        NS_ABORT_MSG_IF(flowState == nullptr || sizeAwareLoadView == nullptr,
                        "size-aware policy 缺少 routing state");
        return std::make_unique<SizeAwareHrwPolicy>(*flowState, *sizeAwareLoadView);
    case RoutingMode::CAPACITY_AWARE_HRW:
        return nullptr;
    }
    return nullptr;
}

std::unique_ptr<PathPolicy>
RoutingPolicyFactory::CreatePathPolicy(RoutingMode mode,
                                       const CapacityAwarePathView* pathView,
                                       const CapacityReservationState* reservationState)
{
    if (mode != RoutingMode::CAPACITY_AWARE_HRW)
    {
        return nullptr;
    }
    NS_ABORT_MSG_IF(pathView == nullptr || reservationState == nullptr,
                    "capacity-aware policy 缺少 topology 或 reservation state");
    return std::make_unique<CapacityAwareHrwPolicy>(*pathView, *reservationState);
}

} // namespace ns3
