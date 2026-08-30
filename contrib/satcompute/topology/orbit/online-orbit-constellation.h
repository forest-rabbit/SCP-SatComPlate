/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ONLINE_ORBIT_CONSTELLATION_H
#define SATCOMPUTE_ONLINE_ORBIT_CONSTELLATION_H

#include "constellation-definition.h"
#include "../satellite-id-map.h"

#include "ns3/leo-circular-orbit-mobility-model.h"
#include "ns3/node-container.h"
#include "ns3/vector.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ns3
{

class OnlineOrbitConstellationError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** One on-demand ECEF position produced by the ns-3 mobility model. */
struct SatelliteEcefPosition
{
    uint32_t satelliteId{};
    Vector positionM;
};

/**
 * Build one deterministic circular-orbit constellation using ns-3.48 mobility.
 *
 * Satellite IDs are always plane-major: plane * satellites-per-orbit + slot.
 * Positions remain continuous; callers decide when network state is sampled.
 */
class OnlineOrbitConstellation
{
  public:
    explicit OnlineOrbitConstellation(const ConstellationDefinition& config,
                                      double startOffsetSeconds = 0.0);

    const ConstellationDefinition& GetConfig() const;
    double GetStartOffsetSeconds() const;
    const NodeContainer& GetNodes() const;
    const SatelliteIdMap& GetIdMap() const;
    Ptr<LeoCircularOrbitMobilityModel> GetMobilityModel(uint32_t satelliteId) const;
    Vector GetPosition(uint32_t satelliteId) const;
    /**
     * Return one deterministic native-orbit position without advancing simulation time.
     *
     * @param satelliteId Stable external satellite ID.
     * @param simulationTime Absolute ns-3 simulation time.
     * @return ECEF position in meters at simulationTime.
     */
    Vector GetPositionAt(uint32_t satelliteId, Time simulationTime) const;
    std::vector<SatelliteEcefPosition> GetPositions() const;

  private:
    ConstellationDefinition m_config;
    double m_startOffsetSeconds{};
    NodeContainer m_nodes;
    std::unique_ptr<SatelliteIdMap> m_idMap;
    std::vector<Ptr<LeoCircularOrbitMobilityModel>> m_mobilityModels;
};

} // namespace ns3

#endif // SATCOMPUTE_ONLINE_ORBIT_CONSTELLATION_H
