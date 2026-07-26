#ifndef SATCOMPUTE_TOPO_JSON_H
#define SATCOMPUTE_TOPO_JSON_H

#include "topo-data.h"

#include <string>

namespace ns3 {

SatelliteSnapshot ReadSatelliteSnapshot(const std::string& nodesFilename,
                                        const std::string& linksFilename);

SnapshotSchedule ScanSatelliteSnapshots(const std::string& directory,
                                         double simulationDurationSeconds);

} // namespace ns3

#endif
