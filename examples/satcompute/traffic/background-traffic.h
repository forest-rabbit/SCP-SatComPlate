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

#ifndef SATCOMPUTE_BACKGROUND_TRAFFIC_H
#define SATCOMPUTE_BACKGROUND_TRAFFIC_H

#include "../metrics/metrics.h"
#include "../para.h"
#include "../topo.h"

#include "ns3/packet-sink.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <vector>

namespace ns3 {

struct ApplicationState
{
  std::vector<Ptr<PacketSink>> sinks;
  uint32_t clientCount = 0;
  uint64_t plannedPacketCount = 0;
};

ApplicationState InstallApplications(const SatComputeConfig& config,
                                     const SatelliteTopology& topology);

TaskApplicationMetrics
CollectApplicationMetrics(const ApplicationState& applications);

} // namespace ns3

#endif
