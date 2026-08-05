/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "online-orbit-constellation.h"

#include "ns3/nstime.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <string>

namespace ns3
{

namespace
{

constexpr double SECONDS_PER_DAY = 86400.0;

double
GetRaanSpanDeg(const std::string& pattern)
{
    if (pattern == "walker-star")
    {
        return 180.0;
    }
    if (pattern == "walker-delta")
    {
        return 360.0;
    }
    throw OnlineOrbitConstellationError("unsupported constellation pattern " + pattern);
}

} // namespace

OnlineOrbitConstellation::OnlineOrbitConstellation(const ConstellationDefinition& config,
                                                   int64_t simulationStartTimeNs)
    : m_config(config)
{
    if (m_config.numOrbits == 0 || m_config.satellitesPerOrbit == 0)
    {
        throw OnlineOrbitConstellationError("online orbit dimensions must be positive");
    }
    const uint64_t satelliteCount64 =
        static_cast<uint64_t>(m_config.numOrbits) * m_config.satellitesPerOrbit;
    if (satelliteCount64 > std::numeric_limits<uint32_t>::max())
    {
        throw OnlineOrbitConstellationError("online orbit satellite count exceeds uint32");
    }
    if (m_config.altitudeM <= 0.0L || !std::isfinite(m_config.altitudeM) ||
        m_config.inclinationDeg < 0.0L || m_config.inclinationDeg >= 180.0L ||
        !std::isfinite(m_config.inclinationDeg) || m_config.orbitEpochOffsetNs < 0 ||
        simulationStartTimeNs < 0 ||
        simulationStartTimeNs >
            std::numeric_limits<int64_t>::max() - m_config.orbitEpochOffsetNs)
    {
        throw OnlineOrbitConstellationError("online orbit parameters are outside their domains");
    }

    const uint32_t satelliteCount = static_cast<uint32_t>(satelliteCount64);
    m_nodes.Create(satelliteCount);
    std::vector<uint32_t> satelliteIds(satelliteCount);
    std::iota(satelliteIds.begin(), satelliteIds.end(), 0);
    m_idMap = std::make_unique<SatelliteIdMap>(m_nodes, satelliteIds);
    m_identities.reserve(satelliteCount);
    m_mobilityModels.reserve(satelliteCount);

    const double raanSpanDeg = GetRaanSpanDeg(m_config.constellationPattern);
    const double slotSpanDeg = 360.0 / m_config.satellitesPerOrbit;
    const double halfSlotDeg = m_config.phaseDiff ? slotSpanDeg / 2.0 : 0.0;
    const int64_t effectiveEpochNs =
        m_config.orbitEpochOffsetNs + simulationStartTimeNs;
    const double epochSeconds = NanoSeconds(effectiveEpochNs).GetSeconds();
    const double earthRotationRadPerSecond = 2.0 * std::numbers::pi / SECONDS_PER_DAY;

    for (uint32_t plane = 0; plane < m_config.numOrbits; ++plane)
    {
        const double raanDeg = plane * raanSpanDeg / m_config.numOrbits;
        const double planePhaseDeg = plane % 2 == 1 ? halfSlotDeg : 0.0;
        for (uint32_t slot = 0; slot < m_config.satellitesPerOrbit; ++slot)
        {
            const uint32_t satelliteId = plane * m_config.satellitesPerOrbit + slot;
            const double baseArgumentLatitudeDeg = slot * slotSpanDeg + planePhaseDeg;

            Ptr<LeoCircularOrbitMobilityModel> mobility =
                CreateObject<LeoCircularOrbitMobilityModel>();
            mobility->SetAltitude(static_cast<double>(m_config.altitudeM));
            mobility->SetInclination(static_cast<double>(m_config.inclinationDeg));
            mobility->SetAttribute("Resolution", TimeValue(Seconds(0)));
            m_nodes.Get(satelliteId)->AggregateObject(mobility);

            const double initialLongitudeDeg =
                raanDeg -
                earthRotationRadPerSecond * epochSeconds * 180.0 / std::numbers::pi;
            const double initialArgumentLatitudeDeg =
                baseArgumentLatitudeDeg +
                mobility->GetAngularVelocity() * epochSeconds * 180.0 / std::numbers::pi;
            mobility->SetPosition(
                Vector(initialLongitudeDeg, initialArgumentLatitudeDeg, satelliteId));

            m_identities.push_back({satelliteId,
                                    plane,
                                    slot,
                                    raanDeg,
                                    baseArgumentLatitudeDeg,
                                    initialLongitudeDeg,
                                    initialArgumentLatitudeDeg});
            m_mobilityModels.push_back(mobility);
        }
    }
}

const ConstellationDefinition&
OnlineOrbitConstellation::GetConfig() const
{
    return m_config;
}

const NodeContainer&
OnlineOrbitConstellation::GetNodes() const
{
    return m_nodes;
}

const SatelliteIdMap&
OnlineOrbitConstellation::GetIdMap() const
{
    return *m_idMap;
}

const std::vector<SatelliteOrbitIdentity>&
OnlineOrbitConstellation::GetOrbitIdentities() const
{
    return m_identities;
}

Ptr<LeoCircularOrbitMobilityModel>
OnlineOrbitConstellation::GetMobilityModel(uint32_t satelliteId) const
{
    return m_mobilityModels.at(m_idMap->GetNodeIndexBySatelliteId(satelliteId));
}

Vector
OnlineOrbitConstellation::GetPosition(uint32_t satelliteId) const
{
    return GetMobilityModel(satelliteId)->GetPosition();
}

std::vector<SatelliteEcefPosition>
OnlineOrbitConstellation::GetPositions() const
{
    std::vector<SatelliteEcefPosition> positions;
    positions.reserve(m_idMap->GetNodeCount());
    for (uint32_t satelliteId : m_idMap->GetCanonicalSatelliteIds())
    {
        positions.push_back({satelliteId, GetPosition(satelliteId)});
    }
    return positions;
}

} // namespace ns3
