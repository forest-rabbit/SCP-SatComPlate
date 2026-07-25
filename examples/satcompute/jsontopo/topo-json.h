#ifndef SATCOMPUTE_TOPO_JSON_H
#define SATCOMPUTE_TOPO_JSON_H

#include "topo-data.h"

#include <cstdint>
#include <string>

namespace ns3 {

SatelliteSnapshot ReadSatelliteSnapshot(const std::string& filename);

SnapshotSchedule ScanSatelliteSnapshots(const std::string& directory,
                                         double simulationDurationSeconds);

bool TryParseSnapshotTimestamp(const std::string& value, int64_t& timestampSeconds);

} // namespace ns3

#endif
