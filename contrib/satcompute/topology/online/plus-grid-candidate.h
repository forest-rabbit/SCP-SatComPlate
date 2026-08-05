/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_PLUS_GRID_CANDIDATE_H
#define SATCOMPUTE_PLUS_GRID_CANDIDATE_H

#include "../orbit/constellation-definition.h"
#include "../orbit/online-orbit-constellation.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ns3
{

class PlusGridCandidateError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

enum class PlusGridCandidateKind
{
    INTRA_PLANE,
    INTER_PLANE,
};

/** One canonical, undirected, fixed candidate ISL. */
struct PlusGridCandidateLink
{
    uint32_t sourceId{};
    uint32_t destinationId{};
    PlusGridCandidateKind kind{PlusGridCandidateKind::INTRA_PLANE};

    bool operator==(const PlusGridCandidateLink&) const = default;
};

/**
 * Build the fixed plus-grid graph from the positions at simulation time zero.
 *
 * Every satellite keeps its two intra-plane ring neighbors. Each pair of
 * adjacent planes uses the minimum-total-distance cyclic one-to-one offset;
 * the resulting satellite identities remain fixed for the whole simulation.
 */
std::vector<PlusGridCandidateLink> BuildPlusGridCandidateLinks(
    const ConstellationDefinition& constellation,
    const std::vector<SatelliteEcefPosition>& initialPositions);

} // namespace ns3

#endif // SATCOMPUTE_PLUS_GRID_CANDIDATE_H
