/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SNAPSHOT_READER_H
#define SATCOMPUTE_SNAPSHOT_READER_H

#include "snapshot-types.h"

#include <filesystem>
#include <stdexcept>

namespace ns3
{

class TopologySnapshotError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Read and strictly validate one legacy-compatible topology snapshot pair.
 *
 * Legacy input stores delay in microseconds and link bandwidth in kilobits per
 * second. The returned structure converts delay to integer nanoseconds and
 * bandwidth to bits per second exactly once.
 *
 * @param nodesFilename JSON file containing the complete satellite ID set.
 * @param linksFilename JSON file containing the complete active ISL set.
 * @return Canonically sorted satellite IDs and undirected links.
 * @throws TopologySnapshotError for I/O, JSON, schema, or endpoint errors.
 */
SatelliteSnapshot ReadSatelliteSnapshot(const std::filesystem::path& nodesFilename,
                                        const std::filesystem::path& linksFilename);

} // namespace ns3

#endif // SATCOMPUTE_SNAPSHOT_READER_H
