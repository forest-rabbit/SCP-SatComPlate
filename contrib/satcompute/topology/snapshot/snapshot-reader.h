/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SNAPSHOT_READER_H
#define SATCOMPUTE_SNAPSHOT_READER_H

#include "snapshot-types.h"

#include <filesystem>
#include <optional>
#include <stdexcept>

namespace ns3
{

class TopologySnapshotError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Read and strictly validate one legacy or self-describing 0.2 snapshot pair.
 *
 * Legacy input stores delay in microseconds and link bandwidth in kilobits per
 * second. Version 0.2 input already stores ECEF coordinates in metres, delay
 * in integer nanoseconds, and bandwidth in bits per second. A pair must use one
 * encoding; version 0.2 embedded timestamps must match each other and the
 * optional schedule timestamp.
 *
 * @param nodesFilename JSON file containing the complete satellite ID set.
 * @param linksFilename JSON file containing the complete active ISL set.
 * @param expectedTimeNs Optional exact time selected from the filenames.
 * @return Canonically sorted satellite IDs and undirected links.
 * @throws TopologySnapshotError for I/O, JSON, schema, or endpoint errors.
 */
SatelliteSnapshot ReadSatelliteSnapshot(const std::filesystem::path& nodesFilename,
                                        const std::filesystem::path& linksFilename,
                                        std::optional<int64_t> expectedTimeNs = std::nullopt);

} // namespace ns3

#endif // SATCOMPUTE_SNAPSHOT_READER_H
