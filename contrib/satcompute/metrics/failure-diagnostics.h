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

#ifndef SATCOMPUTE_FAILURE_DIAGNOSTICS_H
#define SATCOMPUTE_FAILURE_DIAGNOSTICS_H

#include "flow-metrics.h"

#include <string>
#include <vector>

namespace ns3 {

class TaskCoordinator;

void RemoveFailureDiagnosticOutputs(const std::string& outputDirectory);

void WriteFailureDiagnostics(
  const FlowAggregate& aggregate,
  double simulationDurationSeconds,
  const RunMetadata& runMetadata,
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::vector<TransferSummaryRecord>& transferSummaries,
  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
  const std::vector<IslDirectedLink>& directedLinks,
  const std::vector<IslQueueDropEvent>& queueDropEvents,
  const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
  const TaskCoordinator& coordinator,
  const std::string& outputDirectory);

} // namespace ns3

#endif
