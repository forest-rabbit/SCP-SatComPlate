/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SNAPSHOT_TYPES_H
#define SATCOMPUTE_SNAPSHOT_TYPES_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ns3
{

/** One canonical undirected satellite link from a replay snapshot. */
struct SatelliteLink
{
    uint32_t sourceId{};
    uint32_t destinationId{};
    int64_t delayNs{};
    uint64_t bandwidthBps{};

    bool operator==(const SatelliteLink&) const = default;
};

/** Replay input encoding selected from the closed-world root fields. */
enum class SatelliteSnapshotSchema
{
    LEGACY,
    VERSION_0_2,
};

/** One stable ECEF position carried by a version 0.2 node slice. */
struct SatelliteSnapshotPosition
{
    uint32_t satelliteId{};
    double xM{};
    double yM{};
    double zM{};

    bool operator==(const SatelliteSnapshotPosition&) const = default;
};

/** One complete legacy or self-describing 0.2 topology snapshot. */
struct SatelliteSnapshot
{
    SatelliteSnapshotSchema schema{SatelliteSnapshotSchema::LEGACY};
    std::optional<int64_t> simulationTimeNs;
    std::vector<uint32_t> satelliteIds;
    std::vector<SatelliteSnapshotPosition> positions;
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
    bool manifestAuthoritative{};
};

} // namespace ns3

#endif // SATCOMPUTE_SNAPSHOT_TYPES_H
