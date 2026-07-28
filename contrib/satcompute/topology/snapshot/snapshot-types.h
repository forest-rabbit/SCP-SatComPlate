#ifndef SATCOMPUTE_SNAPSHOT_TYPES_H
#define SATCOMPUTE_SNAPSHOT_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

struct SatelliteLink
{
  uint32_t sourceId;
  uint32_t destinationId;
  uint64_t delayUs;
  uint64_t bandwidthBps;

  SatelliteLink()
    : sourceId(0),
      destinationId(0),
      delayUs(0),
      bandwidthBps(0)
  {
  }
};

struct SatelliteSnapshot
{
  std::vector<uint32_t> satelliteIds;
  std::vector<SatelliteLink> links;
};

struct SnapshotUpdate
{
  double timeSeconds;
  std::string nodesFilename;
  std::string linksFilename;

  SnapshotUpdate()
    : timeSeconds(0.0)
  {
  }
};

struct SnapshotSchedule
{
  std::string initialNodesFilename;
  std::string initialLinksFilename;
  std::vector<SnapshotUpdate> updates;
  uint32_t discoveredSnapshotCount;
  uint32_t selectedSnapshotCount;

  SnapshotSchedule()
    : discoveredSnapshotCount(0),
      selectedSnapshotCount(0)
  {
  }
};

} // namespace ns3

#endif
