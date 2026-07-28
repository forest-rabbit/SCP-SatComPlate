#ifndef SATCOMPUTE_SNAPSHOT_SCHEDULE_H
#define SATCOMPUTE_SNAPSHOT_SCHEDULE_H

#include "snapshot-types.h"

#include <string>

namespace ns3 {

SnapshotSchedule ScanSatelliteSnapshots(const std::string& directory,
                                        double simulationDurationSeconds);

} // namespace ns3

#endif
