/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FLOW_DROP_REASON_DIAGNOSTICS_H
#define SATCOMPUTE_FLOW_DROP_REASON_DIAGNOSTICS_H

#include "../core/flow-metrics.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/ptr.h"

#include <string>
#include <vector>

namespace ns3
{

void WriteFlowDropReasons(Ptr<FlowMonitor> monitor,
                          const std::vector<TransferFlowMetadata>& transferFlows,
                          const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_FLOW_DROP_REASON_DIAGNOSTICS_H
