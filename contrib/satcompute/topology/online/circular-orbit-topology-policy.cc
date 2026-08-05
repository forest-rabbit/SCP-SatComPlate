/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "circular-orbit-topology-policy.h"

#include "ns3/simulator.h"

#include <cmath>
#include <limits>
#include <string>

namespace ns3
{

namespace
{

double
CalculateEcefDistance(const Vector& first, const Vector& second)
{
    const double x = first.x - second.x;
    const double y = first.y - second.y;
    const double z = first.z - second.z;
    const double distance = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(distance))
    {
        throw CircularOrbitTopologyPolicyError("candidate ECEF distance is not finite");
    }
    return distance;
}

std::vector<SatelliteLink>
SelectLinks(const CircularOrbitTopologyState& state,
            uint64_t bandwidthBps,
            bool activeOnly)
{
    if (bandwidthBps == 0)
    {
        throw CircularOrbitTopologyPolicyError("candidate bandwidth must be positive");
    }
    std::vector<SatelliteLink> links;
    links.reserve(state.evaluatedLinks.size());
    for (const EvaluatedSatelliteLink& evaluated : state.evaluatedLinks)
    {
        if (!activeOnly || evaluated.active)
        {
            links.push_back({evaluated.sourceId,
                             evaluated.destinationId,
                             evaluated.delayNs,
                             bandwidthBps});
        }
    }
    return links;
}

} // namespace

std::vector<SatelliteLink>
CircularOrbitTopologyState::GetCandidateLinks(uint64_t bandwidthBps) const
{
    return SelectLinks(*this, bandwidthBps, false);
}

std::vector<SatelliteLink>
CircularOrbitTopologyState::GetActiveLinks(uint64_t bandwidthBps) const
{
    return SelectLinks(*this, bandwidthBps, true);
}

int64_t
DistanceToPropagationDelayNs(double distanceM)
{
    if (!std::isfinite(distanceM) || distanceM < 0.0)
    {
        throw CircularOrbitTopologyPolicyError(
            "propagation distance must be finite and non-negative");
    }
    const long double delayNs =
        static_cast<long double>(distanceM) * 1000000000.0L /
        SATCOMPUTE_SPEED_OF_LIGHT_M_PER_S;
    if (delayNs > static_cast<long double>(std::numeric_limits<int64_t>::max()) - 0.5L)
    {
        throw CircularOrbitTopologyPolicyError(
            "propagation delay exceeds signed integer nanoseconds");
    }
    return static_cast<int64_t>(std::floor(delayNs + 0.5L));
}

CircularOrbitTopologyPolicy::CircularOrbitTopologyPolicy(
    const ResolvedSatComputeConfig& config)
    : m_config(config),
      m_candidates(BuildPlusGridCandidateLinks(config.constellation,
                                               config.network.seamEnabled))
{
    if (m_config.network.topologySource != "online")
    {
        throw CircularOrbitTopologyPolicyError(
            "circular-orbit topology policy requires an online topology source");
    }
    if (m_config.network.islCandidateStrategy != "plus-grid")
    {
        throw CircularOrbitTopologyPolicyError(
            "circular-orbit topology policy requires plus-grid candidates");
    }
    if (m_config.network.maxIslDistanceM <= 0.0L ||
        !std::isfinite(m_config.network.maxIslDistanceM))
    {
        throw CircularOrbitTopologyPolicyError("maximum ISL distance must be positive");
    }
    if (m_config.network.delayMode == "fixed")
    {
        if (!m_config.network.fixedDelayNs || *m_config.network.fixedDelayNs <= 0)
        {
            throw CircularOrbitTopologyPolicyError(
                "fixed topology policy requires a positive fixed delay");
        }
    }
    else if (m_config.network.delayMode != "distance" ||
             m_config.network.fixedDelayNs)
    {
        throw CircularOrbitTopologyPolicyError(
            "distance topology policy cannot contain a fixed delay");
    }
}

const std::vector<PlusGridCandidateLink>&
CircularOrbitTopologyPolicy::GetCandidates() const
{
    return m_candidates;
}

CircularOrbitTopologyState
CircularOrbitTopologyPolicy::EvaluateCurrent(
    const OnlineOrbitConstellation& constellation) const
{
    return EvaluatePositions(Simulator::Now().GetNanoSeconds(), constellation.GetPositions());
}

CircularOrbitTopologyState
CircularOrbitTopologyPolicy::EvaluatePositions(
    int64_t simulationTimeNs,
    const std::vector<SatelliteEcefPosition>& positions) const
{
    if (simulationTimeNs < 0)
    {
        throw CircularOrbitTopologyPolicyError("topology evaluation time must be non-negative");
    }
    if (positions.size() != m_config.constellation.GetSatelliteCount())
    {
        throw CircularOrbitTopologyPolicyError(
            "topology position count differs from the constellation");
    }
    for (uint32_t satelliteId = 0; satelliteId < positions.size(); ++satelliteId)
    {
        const SatelliteEcefPosition& position = positions[satelliteId];
        if (position.satelliteId != satelliteId)
        {
            throw CircularOrbitTopologyPolicyError(
                "topology positions must use canonical plane-major ID order");
        }
        if (!std::isfinite(position.positionM.x) || !std::isfinite(position.positionM.y) ||
            !std::isfinite(position.positionM.z))
        {
            throw CircularOrbitTopologyPolicyError(
                "topology position contains a non-finite ECEF coordinate");
        }
    }

    CircularOrbitTopologyState state;
    state.simulationTimeNs = simulationTimeNs;
    state.positions = positions;
    state.evaluatedLinks.reserve(m_candidates.size());
    for (const PlusGridCandidateLink& candidate : m_candidates)
    {
        const double distanceM = CalculateEcefDistance(
            positions[candidate.sourceId].positionM,
            positions[candidate.destinationId].positionM);
        const int64_t delayNs = m_config.network.delayMode == "fixed"
                                    ? *m_config.network.fixedDelayNs
                                    : DistanceToPropagationDelayNs(distanceM);
        state.evaluatedLinks.push_back(
            {candidate.sourceId,
             candidate.destinationId,
             candidate.kind,
             distanceM,
             delayNs,
             static_cast<long double>(distanceM) <= m_config.network.maxIslDistanceM});
    }
    return state;
}

} // namespace ns3
