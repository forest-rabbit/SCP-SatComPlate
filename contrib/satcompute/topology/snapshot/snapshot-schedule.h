/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SNAPSHOT_SCHEDULE_H
#define SATCOMPUTE_SNAPSHOT_SCHEDULE_H

#include "snapshot-types.h"

#include <cstdint>
#include <filesystem>

namespace ns3
{

/**
 * Discover paired replay files and select the scenario network-update cadence.
 *
 * Snapshot filenames use nodes_<seconds>s.json and topology_<seconds>s.json.
 * When a version 0.2 manifest.json exists, its ordered inventory is
 * authoritative and every listed SHA-256 is verified; unlisted stale files are
 * ignored. Legacy directories without a manifest retain filename discovery.
 * Seconds are parsed exactly to integer nanoseconds. A directory may contain a
 * finer cadence than the simulation consumes; only t = 0, interval, 2*interval
 * and so on strictly before the stop time are selected. Every selected time
 * must exist as a complete pair.
 *
 * @param directory Directory containing snapshot pairs.
 * @param simulationDurationNs Positive simulation duration in nanoseconds.
 * @param networkUpdateIntervalNs Positive replay cadence in nanoseconds.
 * @return Deterministic initial files, updates, and discovery counts.
 * @throws TopologySnapshotError for invalid directories, names, pairs, or gaps.
 */
SnapshotSchedule ScanSatelliteSnapshots(const std::filesystem::path& directory,
                                        int64_t simulationDurationNs,
                                        int64_t networkUpdateIntervalNs);

} // namespace ns3

#endif // SATCOMPUTE_SNAPSHOT_SCHEDULE_H
