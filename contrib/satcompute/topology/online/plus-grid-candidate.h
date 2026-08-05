/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_PLUS_GRID_CANDIDATE_H
#define SATCOMPUTE_PLUS_GRID_CANDIDATE_H

#include "../orbit/constellation-definition.h"

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

/** Build the canonical plus-grid graph without consulting satellite distance. */
std::vector<PlusGridCandidateLink> BuildPlusGridCandidateLinks(
    const ConstellationDefinition& constellation,
    bool seamEnabled);

} // namespace ns3

#endif // SATCOMPUTE_PLUS_GRID_CANDIDATE_H
