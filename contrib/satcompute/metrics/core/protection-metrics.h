/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_METRICS_H
#define SATCOMPUTE_PROTECTION_METRICS_H
#include "../../protection/mechanism/checkpoint/checkpoint-manager.h"
#include <filesystem>

namespace ns3
{
/** Write only when protection is enabled; normal INPUT/RESULT summaries stay separate. */
void WriteProtectionMetrics(const protection::CheckpointManager& manager,
                            const NetworkTransferEngine& network,
                            const std::filesystem::path& directory);
/** Remove only the four known generated outputs on a subsequent off run. */
void RemoveProtectionMetrics(const std::filesystem::path& directory);
} // namespace ns3
#endif
