/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// 为下一跳级路由模式创建独立策略实例。

#include "routing-policy-factory.h"

#include "algorithm/global-first-policy.h"
#include "algorithm/hash-per-flow-policy.h"
#include "algorithm/hrw-per-flow-policy.h"

namespace ns3 {

std::unique_ptr<NextHopPolicy>
RoutingPolicyFactory::CreateNextHopPolicy(RoutingMode mode)
{
  switch (mode)
    {
    case RoutingMode::GLOBAL_FIRST:
      return std::unique_ptr<NextHopPolicy>(new GlobalFirstPolicy());
    case RoutingMode::HASH_PER_FLOW:
      return std::unique_ptr<NextHopPolicy>(new HashPerFlowPolicy());
    case RoutingMode::HRW_PER_FLOW:
      return std::unique_ptr<NextHopPolicy>(new HrwPerFlowPolicy());
    case RoutingMode::SIZE_AWARE_HRW:
    case RoutingMode::CAPACITY_AWARE_HRW:
      return std::unique_ptr<NextHopPolicy>();
    }
  return std::unique_ptr<NextHopPolicy>();
}

} // namespace ns3
