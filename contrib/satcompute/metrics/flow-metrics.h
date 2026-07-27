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

#ifndef SATCOMPUTE_FLOW_METRICS_H
#define SATCOMPUTE_FLOW_METRICS_H

#include "metrics.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

struct FlowAggregate
{
  uint64_t txPackets = 0;
  uint64_t rxPackets = 0;
  uint64_t lostPackets = 0;
  uint64_t txBytes = 0;
  uint64_t rxBytes = 0;
  uint64_t jitterSamples = 0;
  double delaySumSeconds = 0.0;
  double jitterSumSeconds = 0.0;
  double measurementStartSeconds = 0.0;
  double measurementEndSeconds = 0.0;
  bool hasMeasurement = false;

  void Add(const FlowMonitor::FlowStats& flow);
  double MeasurementDurationSeconds() const;
};

Ptr<FlowMonitor> InstallSimulationFlowMonitor();

FlowAggregate CollectFlowAggregate(Ptr<FlowMonitor> monitor);

void PrintNetworkMetrics(const FlowAggregate& metrics);

void WriteNetworkMetrics(const FlowAggregate& metrics,
                         const std::string& outputDirectory);

void WriteNetworkFlowDetails(
  Ptr<FlowMonitor> monitor,
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::string& outputDirectory,
  bool requireCompleteCoverage);

} // namespace ns3

#endif
