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

#include "para.h"

namespace ns3 {

SatComputeConfig
GetDefaultSatComputeConfig()
{
  SatComputeConfig config;
  config.topologyDirectory =
    "contrib/satcompute/input/topology/json/examples/xw-66sat";
  config.trafficMatrix =
    "contrib/satcompute/input/traffic/csv/traffic_matrix(66).csv";
  config.transferTrace = "";
  config.outputDirectory = "contrib/satcompute/output";
  config.transport = "udp";
  config.routingMode = "global-first";
  config.transferLogMode = "summary";
  config.transferChunkMode = "fixed";
  config.transferPayloadBytes = 1024;
  config.islMtuBytes = 1500;
  config.islQueueBytes = 1500000;
  config.ecmpHashSeed = 1;
  config.simulationDurationSeconds = 110.0;
  config.offeredLoad = 0.0;
  return config;
}

} // namespace ns3
