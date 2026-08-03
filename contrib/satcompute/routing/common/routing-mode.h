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

#ifndef SATCOMPUTE_ROUTING_MODE_H
#define SATCOMPUTE_ROUTING_MODE_H

#include <string>

namespace ns3 {

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
