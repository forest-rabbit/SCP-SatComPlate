/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 安装和访问 SatCompute 全局路由，并在拓扑更新后推进 route epoch。

#include "satcompute-ipv4-global-routing-helper.h"

#include "ns3/abort.h"
#include "ns3/global-router-interface.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"

namespace ns3
{

SatComputeIpv4GlobalRoutingHelper::SatComputeIpv4GlobalRoutingHelper(
    RoutingMode selectionMode,
    uint64_t hashSeed,
    Ptr<FlowRouteRegistry> flowRouteRegistry)
    : m_selectionMode(selectionMode), m_hashSeed(hashSeed),
      m_flowRouteRegistry(flowRouteRegistry)
{
}

SatComputeIpv4GlobalRoutingHelper::SatComputeIpv4GlobalRoutingHelper(
    const SatComputeIpv4GlobalRoutingHelper& other)
    : m_selectionMode(other.m_selectionMode), m_hashSeed(other.m_hashSeed),
      m_flowRouteRegistry(other.m_flowRouteRegistry)
{
}

SatComputeIpv4GlobalRoutingHelper*
SatComputeIpv4GlobalRoutingHelper::Copy() const
{
    return new SatComputeIpv4GlobalRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
SatComputeIpv4GlobalRoutingHelper::Create(Ptr<Node> node) const
{
    NS_ABORT_MSG_IF(node->GetObject<Ipv4GlobalRouter>() != nullptr,
                    "节点已经聚合 GlobalRouter: " << node->GetId());
    Ptr<Ipv4GlobalRouter> globalRouter = CreateObject<Ipv4GlobalRouter>();
    node->AggregateObject(globalRouter);

    Ptr<SatComputeIpv4GlobalRouting> routing = CreateObject<SatComputeIpv4GlobalRouting>();
    routing->Configure(m_selectionMode, m_hashSeed, m_flowRouteRegistry);
    globalRouter->SetRoutingProtocol(routing);
    return routing;
}

Ptr<SatComputeIpv4GlobalRouting>
SatComputeIpv4GlobalRoutingHelper::GetRouting(Ptr<Node> node)
{
    NS_ABORT_MSG_IF(node == nullptr, "无法从空节点读取 SatCompute routing");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ABORT_MSG_IF(ipv4 == nullptr, "节点缺少 Ipv4: " << node->GetId());
    Ptr<SatComputeIpv4GlobalRouting> routing =
        Ipv4RoutingHelper::GetRouting<SatComputeIpv4GlobalRouting>(ipv4->GetRoutingProtocol());
    NS_ABORT_MSG_IF(routing == nullptr, "节点缺少 SatComputeIpv4GlobalRouting: " << node->GetId());
    return routing;
}

void
SatComputeIpv4GlobalRoutingHelper::AdvanceRouteEpoch(const NodeContainer& nodes)
{
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        GetRouting(nodes.Get(index))->AdvanceRouteEpoch();
    }
}

void
SatComputeIpv4GlobalRoutingHelper::InvalidateDecisionCache(const NodeContainer& nodes,
                                                           const EcmpFlowKey& flowKey)
{
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        GetRouting(nodes.Get(index))->InvalidateDecisionCache(flowKey);
    }
}

} // namespace ns3
