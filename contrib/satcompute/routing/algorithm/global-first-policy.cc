/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 表示完全委托给 ns-3 Ipv4GlobalRouting 的原生首条路由行为。

#include "global-first-policy.h"

namespace ns3
{

NextHopDecision
GlobalFirstPolicy::Select(const NextHopSelectionContext&, const std::vector<EcmpRouteCandidate>&)
{
    return {true, 0, 0, "GLOBAL_FIRST_NATIVE"};
}

} // namespace ns3
