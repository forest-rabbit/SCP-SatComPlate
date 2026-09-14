/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PLACEMENT_RESOURCES_H
#define SATCOMPUTE_PLACEMENT_RESOURCES_H
#include <cstdint>

namespace ns3::protection
{
/** Causal resource observation; history window fields remain available to historical diagnostics. */
struct PlacementResourceSnapshot
{
    uint32_t remoteNode{};
    uint64_t normalBusyNs{}, recoveryBusyNs{}, exposureNs{};
    int64_t historyHorizonNs{}, historyWindowBeginNs{}, historyWindowEndNs{};
    int64_t continuousIdleNs{};
    uint64_t recentNormalBusyNs{}, recentRecoveryBusyNs{}, recentExposureNs{};
    uint64_t capacityBytes{}, accountedBytes{}, additionalQuotaBytes{};
};
/** Assignment-time and storage-byte-time observations, not a placement policy. */
struct PlacementNodeObservation
{
    uint64_t assignments{}, active{}, peakActive{}, storageBytes{}, peakStorage{};
    int64_t assignmentAtNs{}, storageAtNs{};
    unsigned __int128 assignmentNs{}, storageByteNs{};
};

} // namespace ns3::protection
#endif
