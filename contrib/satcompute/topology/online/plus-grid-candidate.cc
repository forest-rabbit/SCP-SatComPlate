/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "plus-grid-candidate.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace ns3
{

std::vector<PlusGridCandidateLink>
BuildPlusGridCandidateLinks(const ConstellationDefinition& constellation, bool seamEnabled)
{
    if (constellation.numOrbits == 0 || constellation.satellitesPerOrbit == 0)
    {
        throw PlusGridCandidateError("plus-grid dimensions must be positive");
    }
    const uint64_t satelliteCount64 =
        static_cast<uint64_t>(constellation.numOrbits) * constellation.satellitesPerOrbit;
    if (satelliteCount64 > std::numeric_limits<uint32_t>::max())
    {
        throw PlusGridCandidateError("plus-grid satellite count exceeds uint32");
    }

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

    const uint32_t satellitesPerOrbit = constellation.satellitesPerOrbit;
    for (uint32_t plane = 0; plane < constellation.numOrbits; ++plane)
    {
        const uint32_t planeStart = plane * satellitesPerOrbit;
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            addCandidate(planeStart + slot,
                         planeStart + (slot + 1) % satellitesPerOrbit,
                         PlusGridCandidateKind::INTRA_PLANE);
        }
    }

    for (uint32_t plane = 0; plane + 1 < constellation.numOrbits; ++plane)
    {
        const uint32_t firstPlaneStart = plane * satellitesPerOrbit;
        const uint32_t secondPlaneStart = (plane + 1) * satellitesPerOrbit;
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            addCandidate(firstPlaneStart + slot,
                         secondPlaneStart + slot,
                         PlusGridCandidateKind::INTER_PLANE);
        }
    }

    if (seamEnabled && constellation.numOrbits > 1)
    {
        const uint32_t lastPlaneStart = (constellation.numOrbits - 1) * satellitesPerOrbit;
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            addCandidate(lastPlaneStart + slot,
                         slot,
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
