/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "online-orbit-constellation.h"

#include "ns3/leo-orbit-node-helper.h"
#include "ns3/leo-circular-orbit-position-allocator.h"
#include "ns3/angles.h"
#include "ns3/nstime.h"

#include <cmath>
#include <limits>
#include <numeric>

namespace ns3
{

OnlineOrbitConstellation::OnlineOrbitConstellation(const ConstellationDefinition& config,
                                                   double startOffsetSeconds)
    : m_config(config),
      m_startOffsetSeconds(startOffsetSeconds)
{
    const LeoOrbitalShell& shell = m_config.shell;
    if (!std::isfinite(shell.alt) || shell.alt <= 0.0 || !std::isfinite(shell.inc) ||
        shell.inc < 0.0 || shell.inc >= 180.0 || shell.planes == 0 || shell.sats == 0 ||
        !std::isfinite(shell.phasing) || shell.phasing < 0.0 ||
        std::floor(shell.phasing) != shell.phasing || shell.phasing >= shell.planes ||
        !std::isfinite(shell.raanSpanDeg) || shell.raanSpanDeg <= 0.0 ||
        shell.raanSpanDeg > 360.0)
    {
        throw OnlineOrbitConstellationError("online orbit shell is invalid");
    }
    if (!std::isfinite(m_startOffsetSeconds) || m_startOffsetSeconds < 0.0)
    {
        throw OnlineOrbitConstellationError(
            "online orbit start offset must be finite and non-negative");
    }
    const uint64_t satelliteCount64 =
        static_cast<uint64_t>(shell.planes) * shell.sats;
    if (satelliteCount64 > std::numeric_limits<uint32_t>::max())
    {
        throw OnlineOrbitConstellationError("online orbit satellite count exceeds uint32");
    }

    LeoOrbitNodeHelper orbitHelper(Seconds(0));
    if (m_config.sourcePath.empty())
    {
        m_nodes = orbitHelper.CreateNodesAndInstallMobility(m_config.shell);
    }
    else
    {
        // Let the ns-3.48 helper consume the same native CSV used by the
        // platform instead of reproducing its position allocator.
        m_nodes = orbitHelper.CreateNodesAndInstallMobility(m_config.sourcePath.string());
    }
    if (m_nodes.GetN() != satelliteCount64)
    {
        throw OnlineOrbitConstellationError(
            "native LEO helper node count differs from the validated shell");
    }

    const uint32_t satelliteCount = static_cast<uint32_t>(satelliteCount64);
    std::vector<uint32_t> satelliteIds(satelliteCount);
    std::iota(satelliteIds.begin(), satelliteIds.end(), 0);
    m_idMap = std::make_unique<SatelliteIdMap>(m_nodes, satelliteIds);
    m_mobilityModels.reserve(satelliteCount);
    for (uint32_t satelliteId = 0; satelliteId < satelliteCount; ++satelliteId)
    {
        Ptr<LeoCircularOrbitMobilityModel> mobility =
            m_nodes.Get(satelliteId)->GetObject<LeoCircularOrbitMobilityModel>();
        if (mobility == nullptr)
        {
            throw OnlineOrbitConstellationError(
                "native LEO helper did not install circular-orbit mobility");
        }
        m_mobilityModels.push_back(mobility);
    }

    if (m_startOffsetSeconds > 0.0)
    {
        Ptr<LeoCircularOrbitAllocator> allocator =
            CreateObject<LeoCircularOrbitAllocator>();
        allocator->SetNumOrbits(shell.planes);
        allocator->SetNumSatellites(shell.sats);
        allocator->SetPhasingFactor(static_cast<uint16_t>(shell.phasing));
        allocator->SetRaanSpanDeg(shell.raanSpanDeg);
        const double earthRotationDegrees =
            m_startOffsetSeconds * 360.0 / Days(1).GetSeconds();
        for (uint32_t satelliteId = 0; satelliteId < satelliteCount; ++satelliteId)
        {
            const LeoOrbitPosition initial = allocator->GetNextOrbitPosition();
            Ptr<LeoCircularOrbitMobilityModel> mobility =
                m_mobilityModels[satelliteId];
            const double orbitalProgressDegrees = RadiansToDegrees(
                mobility->GetAngularVelocity() * m_startOffsetSeconds);
            mobility->SetPosition(
                Vector(initial.longitude - earthRotationDegrees,
                       initial.argumentOfLatitude + orbitalProgressDegrees,
                       initial.satelliteIndex));
        }
    }
}

const ConstellationDefinition&
OnlineOrbitConstellation::GetConfig() const
{
    return m_config;
}

double
OnlineOrbitConstellation::GetStartOffsetSeconds() const
{
    return m_startOffsetSeconds;
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
