/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "plus-grid-candidate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace ns3
{

namespace
{

void
ValidateInitialPositions(const ConstellationDefinition& constellation,
                         const std::vector<SatelliteEcefPosition>& positions)
{
    if (positions.size() != constellation.GetSatelliteCount())
    {
        throw PlusGridCandidateError(
            "initial position count differs from the constellation");
    }
    for (uint32_t satelliteId = 0; satelliteId < positions.size(); ++satelliteId)
    {
        const SatelliteEcefPosition& position = positions[satelliteId];
        if (position.satelliteId != satelliteId)
        {
            throw PlusGridCandidateError(
                "initial positions must use canonical plane-major ID order");
        }
        if (!std::isfinite(position.positionM.x) || !std::isfinite(position.positionM.y) ||
            !std::isfinite(position.positionM.z))
        {
            throw PlusGridCandidateError("initial position contains a non-finite coordinate");
        }
    }
}

long double
Distance(const Vector& first, const Vector& second)
{
    const long double x = static_cast<long double>(first.x) - second.x;
    const long double y = static_cast<long double>(first.y) - second.y;
    const long double z = static_cast<long double>(first.z) - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

uint32_t
FindNearestCyclicOffset(uint32_t firstPlane,
                        uint32_t secondPlane,
                        uint32_t satellitesPerOrbit,
                        const std::vector<SatelliteEcefPosition>& positions)
{
    long double bestTotalDistance = std::numeric_limits<long double>::infinity();
    uint32_t bestOffset = 0;
    for (uint32_t offset = 0; offset < satellitesPerOrbit; ++offset)
    {
        long double totalDistance = 0.0L;
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            const uint32_t firstId = firstPlane * satellitesPerOrbit + slot;
            const uint32_t secondSlot = (slot + offset) % satellitesPerOrbit;
            const uint32_t secondId = secondPlane * satellitesPerOrbit + secondSlot;
            totalDistance +=
                Distance(positions[firstId].positionM, positions[secondId].positionM);
        }
        if (!std::isfinite(totalDistance))
        {
            throw PlusGridCandidateError("initial inter-plane distance sum is not finite");
        }
        // Offsets are visited in ascending order, so an exact tie keeps the
        // smallest offset and makes the matching deterministic.
        if (totalDistance < bestTotalDistance)
        {
            bestTotalDistance = totalDistance;
            bestOffset = offset;
        }
    }
    return bestOffset;
}

} // namespace

std::vector<PlusGridCandidateLink>
BuildPlusGridCandidateLinks(
    const ConstellationDefinition& constellation,
    const std::vector<SatelliteEcefPosition>& initialPositions)
{
    if (constellation.shell.planes == 0 || constellation.shell.sats == 0)
    {
        throw PlusGridCandidateError("plus-grid dimensions must be positive");
    }
    const uint64_t satelliteCount64 =
        static_cast<uint64_t>(constellation.shell.planes) * constellation.shell.sats;
    if (satelliteCount64 > std::numeric_limits<uint32_t>::max())
    {
        throw PlusGridCandidateError("plus-grid satellite count exceeds uint32");
    }
    ValidateInitialPositions(constellation, initialPositions);

    std::map<std::pair<uint32_t, uint32_t>, PlusGridCandidateKind> candidates;
    const auto addCandidate = [&candidates](uint32_t first,
                                            uint32_t second,
                                            PlusGridCandidateKind kind) {
        if (first == second)
        {
            return;
        }
        const auto endpoints = std::minmax(first, second);
        const std::pair<uint32_t, uint32_t> edge(endpoints.first, endpoints.second);
        const auto [position, inserted] = candidates.emplace(edge, kind);
        if (!inserted && position->second != kind)
        {
            throw PlusGridCandidateError("plus-grid edge has conflicting construction kinds");
        }
    };

    const uint32_t satellitesPerOrbit = static_cast<uint32_t>(constellation.shell.sats);
    const uint32_t numOrbits = static_cast<uint32_t>(constellation.shell.planes);
    for (uint32_t plane = 0; plane < numOrbits; ++plane)
    {
        const uint32_t planeStart = plane * satellitesPerOrbit;
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            addCandidate(planeStart + slot,
                         planeStart + (slot + 1) % satellitesPerOrbit,
                         PlusGridCandidateKind::INTRA_PLANE);
        }
    }

    for (uint32_t plane = 0; plane + 1 < numOrbits; ++plane)
    {
        const uint32_t firstPlaneStart = plane * satellitesPerOrbit;
        const uint32_t secondPlaneStart = (plane + 1) * satellitesPerOrbit;
        const uint32_t offset = FindNearestCyclicOffset(plane,
                                                        plane + 1,
                                                        satellitesPerOrbit,
                                                        initialPositions);
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            addCandidate(firstPlaneStart + slot,
                         secondPlaneStart + (slot + offset) % satellitesPerOrbit,
                         PlusGridCandidateKind::INTER_PLANE);
        }
    }

    std::vector<PlusGridCandidateLink> result;
    result.reserve(candidates.size());
    for (const auto& [endpoints, kind] : candidates)
    {
        result.push_back({endpoints.first, endpoints.second, kind});
    }
    return result;
}

} // namespace ns3
