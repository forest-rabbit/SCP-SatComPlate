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

#ifndef SATCOMPUTE_ROUTE_AUDIT_COMMON_H
#define SATCOMPUTE_ROUTE_AUDIT_COMMON_H

#include "ns3/abort.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ns3 {

inline std::vector<uint32_t>
ParseRouteAuditTimes(const std::string& value,
                     double simulationDurationSeconds)
{
  NS_ABORT_MSG_IF(value.empty(), "auditTimes 不能为空");
  std::vector<uint32_t> times;
  std::stringstream input(value);
  std::string token;
  while (std::getline(input, token, ','))
    {
      NS_ABORT_MSG_IF(token.empty(), "auditTimes 包含空字段");
      std::stringstream parser(token);
      uint64_t parsed = 0;
      parser >> parsed;
      NS_ABORT_MSG_IF(
        !parser || parser.peek() != std::char_traits<char>::eof(),
        "auditTimes 包含非整数: " << token);
      NS_ABORT_MSG_IF(parsed > std::numeric_limits<uint32_t>::max(),
                      "auditTimes 超过 uint32_t");
      uint32_t timeSeconds = static_cast<uint32_t>(parsed);
      NS_ABORT_MSG_IF(timeSeconds > simulationDurationSeconds,
                      "audit time 超过 simulationDuration: "
                        << timeSeconds);
      times.push_back(timeSeconds);
    }
  NS_ABORT_MSG_IF(times.empty(), "auditTimes 不能为空");
  NS_ABORT_MSG_IF(!std::is_sorted(times.begin(), times.end()),
                  "auditTimes 必须递增");
  NS_ABORT_MSG_IF(
    std::adjacent_find(times.begin(), times.end()) != times.end(),
    "auditTimes 不能重复");
  return times;
}

} // namespace ns3

#endif
