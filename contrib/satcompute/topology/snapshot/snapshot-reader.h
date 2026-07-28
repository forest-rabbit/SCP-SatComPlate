#ifndef SATCOMPUTE_SNAPSHOT_READER_H
#define SATCOMPUTE_SNAPSHOT_READER_H

#include "snapshot-types.h"

#include <string>

namespace ns3 {

SatelliteSnapshot ReadSatelliteSnapshot(const std::string& nodesFilename,
                                        const std::string& linksFilename);

} // namespace ns3

#endif
