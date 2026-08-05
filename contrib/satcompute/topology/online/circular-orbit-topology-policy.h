/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CIRCULAR_ORBIT_TOPOLOGY_POLICY_H
#define SATCOMPUTE_CIRCULAR_ORBIT_TOPOLOGY_POLICY_H

#include "../link/satellite-link.h"
#include "../orbit/online-orbit-constellation.h"
#include "plus-grid-candidate.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns3
{

inline constexpr uint64_t SATCOMPUTE_SPEED_OF_LIGHT_M_PER_S = 299792458;

class CircularOrbitTopologyPolicyError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** One fixed candidate evaluated against current ECEF positions. */
struct EvaluatedSatelliteLink
{
    uint32_t sourceId{};
    uint32_t destinationId{};
    PlusGridCandidateKind kind{PlusGridCandidateKind::INTRA_PLANE};
    double distanceM{};
    int64_t delayNs{};
    bool active{};
};

/** Complete deterministic orbit/topology state at one simulation time. */
struct CircularOrbitTopologyState
{
    int64_t simulationTimeNs{};
    std::vector<SatelliteEcefPosition> positions;
    std::vector<EvaluatedSatelliteLink> evaluatedLinks;

    std::vector<SatelliteLink> GetCandidateLinks(uint64_t bandwidthBps) const;
    std::vector<SatelliteLink> GetActiveLinks(uint64_t bandwidthBps) const;
};

/** Convert a non-negative one-way distance to nearest integer nanoseconds. */
int64_t DistanceToPropagationDelayNs(double distanceM);

/**
 * Shared online/offline fixed-candidate topology policy.
 *
 * The policy never searches for a nearest replacement. It evaluates only the
 * canonical plus-grid identities constructed at initialization.
 */
class CircularOrbitTopologyPolicy
{
  public:
    /**
     * 构造固定 plus-grid 候选策略。
     *
     * @param constellation 星座结构。
     * @param initialPositions 仿真零时刻的原生轨道坐标。
     * @param maxIslDistanceM 候选链路有效距离门限。
     * @param delayMode `fixed` 或 `distance`。
     * @param fixedDelayNs fixed 模式单向时延；distance 模式为空。
     */
    CircularOrbitTopologyPolicy(const ConstellationDefinition& constellation,
                                const std::vector<SatelliteEcefPosition>& initialPositions,
                                long double maxIslDistanceM,
                                const std::string& delayMode,
                                std::optional<int64_t> fixedDelayNs);

    const std::vector<PlusGridCandidateLink>& GetCandidates() const;
    CircularOrbitTopologyState EvaluateCurrent(
        const OnlineOrbitConstellation& constellation) const;
    CircularOrbitTopologyState EvaluatePositions(
        int64_t simulationTimeNs,
        const std::vector<SatelliteEcefPosition>& positions) const;

  private:
    uint32_t m_satelliteCount{};
    long double m_maxIslDistanceM{};
    std::string m_delayMode;
    std::optional<int64_t> m_fixedDelayNs;
    std::vector<PlusGridCandidateLink> m_candidates;
};

} // namespace ns3

#endif // SATCOMPUTE_CIRCULAR_ORBIT_TOPOLOGY_POLICY_H
