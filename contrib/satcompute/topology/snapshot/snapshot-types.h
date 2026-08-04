/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SNAPSHOT_TYPES_H
#define SATCOMPUTE_SNAPSHOT_TYPES_H

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ns3
{

/** One canonical undirected satellite link from a replay snapshot. */
struct SatelliteLink
{
    uint32_t sourceId{};
    uint32_t destinationId{};
    uint64_t delayUs{};
    uint64_t bandwidthBps{};

    bool operator==(const SatelliteLink&) const = default;
};

/** One complete legacy-compatible satellite topology snapshot. */
struct SatelliteSnapshot
{
    std::vector<uint32_t> satelliteIds;
    std::vector<SatelliteLink> links;
};

/** One replay update selected at an exact simulation timestamp. */
struct SnapshotUpdate
{
    int64_t timeNs{};
    std::filesystem::path nodesFilename;
    std::filesystem::path linksFilename;
};

/** Deterministic replay plan selected from all snapshots in one directory. */
struct SnapshotSchedule
{
    std::filesystem::path initialNodesFilename;
    std::filesystem::path initialLinksFilename;
    std::vector<SnapshotUpdate> updates;
    uint32_t discoveredSnapshotCount{};
    uint32_t selectedSnapshotCount{};
};

} // namespace ns3

#endif // SATCOMPUTE_SNAPSHOT_TYPES_H
