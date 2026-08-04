/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_ONLINE_ORBIT_CONSTELLATION_H
#define SATCOMPUTE_ONLINE_ORBIT_CONSTELLATION_H

#include "../../model/scenario-config.h"
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

/** Stable plane-major identity and initial circular-orbit parameters. */
struct SatelliteOrbitIdentity
{
    uint32_t satelliteId{};
    uint32_t planeIndex{};
    uint32_t slotIndex{};
    double raanDeg{};
    double baseArgumentLatitudeDeg{};
    double initialLongitudeDeg{};
    double initialArgumentLatitudeDeg{};
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
    explicit OnlineOrbitConstellation(const ConstellationConfig& config);

    const ConstellationConfig& GetConfig() const;
    const NodeContainer& GetNodes() const;
    const SatelliteIdMap& GetIdMap() const;
    const std::vector<SatelliteOrbitIdentity>& GetOrbitIdentities() const;
    Ptr<LeoCircularOrbitMobilityModel> GetMobilityModel(uint32_t satelliteId) const;
    Vector GetPosition(uint32_t satelliteId) const;
    std::vector<SatelliteEcefPosition> GetPositions() const;

  private:
    ConstellationConfig m_config;
    NodeContainer m_nodes;
    std::unique_ptr<SatelliteIdMap> m_idMap;
    std::vector<SatelliteOrbitIdentity> m_identities;
    std::vector<Ptr<LeoCircularOrbitMobilityModel>> m_mobilityModels;
};

} // namespace ns3

#endif // SATCOMPUTE_ONLINE_ORBIT_CONSTELLATION_H
