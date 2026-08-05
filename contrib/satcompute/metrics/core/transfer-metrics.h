/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TRANSFER_METRICS_H
#define SATCOMPUTE_TRANSFER_METRICS_H

#include "../../traffic/network-transfer-records.h"

#include <string>
#include <vector>

namespace ns3
{

/** Write the legacy-compatible logical transfer summary. */
void WriteTransferSummaries(const std::vector<TransferSummaryRecord>& summaries,
                            const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_TRANSFER_METRICS_H
