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

#ifndef METRICS_H
#define METRICS_H

#include "ns3/flow-monitor-module.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>

namespace ns3 {

struct TaskApplicationMetrics
{
  uint64_t server_applications;
  uint64_t udp_packets_received;
  uint64_t tcp_bytes_received;
};

Ptr<FlowMonitor> InstallSimulationFlowMonitor();

class MetricsRecorder
{
public:
  MetricsRecorder(Ptr<FlowMonitor> monitor,
                  double wallClockSeconds,
                  const TaskApplicationMetrics& applicationMetrics,
                  const std::string& outputDirectory);
  void Record();

private:
  Ptr<FlowMonitor> m_monitor;
  double m_wallClockSeconds;
  TaskApplicationMetrics m_applicationMetrics;
  std::string m_outputDirectory;
};

} // namespace ns3

#endif
