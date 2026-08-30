/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_METRICS_H
#define SATCOMPUTE_FAULT_METRICS_H

#include "../../traffic/network-transfer-records.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{

class FaultController;
class FaultPredictionEngine;
class TaskCoordinator;

/** Write deterministic fault events and one run-level fault summary. */
void WriteFaultMetrics(const FaultController& controller,
                       const FaultPredictionEngine* predictionEngine,
                       const TaskCoordinator* taskCoordinator,
                       const std::vector<TransferSummaryRecord>& transferSummaries,
                       int64_t simulationDurationNs,
                       const std::string& outputDirectory);

/** Remove only the fault metric files owned by SatCompute. */
void RemoveFaultMetrics(const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_METRICS_H
