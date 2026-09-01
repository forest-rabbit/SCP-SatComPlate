/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/geographic-positions.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/plus-grid-candidate.h"
#include "ns3/simulator.h"

#include "../support/config-factory.h"

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeTestConstellation;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void
CheckClose(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance)
    {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 ", expected=" + std::to_string(expected));
    }
}

double
Distance(const Vector& first, const Vector& second)
{
    const double x = first.x - second.x;
    const double y = first.y - second.y;
    const double z = first.z - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

void
RunIdentityAndMotionCase()
{
    {
        OnlineOrbitConstellation constellation(MakeTestConstellation(3, 4));
        Check(constellation.GetNodes().GetN() == 12, "online orbit node count differs");
        Check(constellation.GetIdMap().GetSatelliteIdByNodeIndex(6) == 6,
              "stable ID map differs");
        Check(constellation.GetNodes().Get(6)->GetObject<LeoCircularOrbitMobilityModel>() ==
                  constellation.GetMobilityModel(6),
              "official mobility model was not aggregated on the satellite node");

        const Vector initial = constellation.GetPosition(0);
        Check(std::isfinite(initial.x) && std::isfinite(initial.y) && std::isfinite(initial.z),
              "online orbit returned a non-finite ECEF position");
        CheckClose(initial.GetLength(),
                   GeographicPositions::EARTH_SPHERE_RADIUS +
                       constellation.GetMobilityModel(0)->GetAltitude(),
                   0.001,
                   "online orbit ECEF radius differs");

        const Vector predictedOneSecond =
            constellation.GetPositionAt(0, Seconds(1));
        double movementM = 0.0;
        double predictionErrorM = 0.0;
        Simulator::Schedule(Seconds(1), [&] {
            const Vector actualOneSecond = constellation.GetPosition(0);
            movementM = Distance(initial, actualOneSecond);
            predictionErrorM = Distance(predictedOneSecond, actualOneSecond);
        });
        Simulator::Stop(Seconds(1));
        Simulator::Run();
        Check(movementM > 1000.0, "official mobility position did not evolve continuously");
        Check(predictionErrorM < 0.001,
              "time-indexed native orbit query differs from runtime position");
    }
    Simulator::Destroy();
}

void
RunStartOffsetCase()
{
    constexpr double startOffsetSeconds = 1234.0;
    const ConstellationDefinition config = MakeTestConstellation(3, 4);
    Vector expected;
    {
        OnlineOrbitConstellation baseline(config);
        Simulator::Schedule(Seconds(startOffsetSeconds), [&] {
            expected = baseline.GetPosition(7);
        });
        Simulator::Stop(Seconds(startOffsetSeconds));
        Simulator::Run();
    }
    Simulator::Destroy();

    {
        OnlineOrbitConstellation shifted(config, startOffsetSeconds);
        Check(shifted.GetStartOffsetSeconds() == startOffsetSeconds,
              "online orbit lost its start offset");
        Check(Distance(expected, shifted.GetPosition(7)) < 0.001,
              "shifted orbit time zero differs from the baseline future position");
    }
    Simulator::Destroy();
}

std::set<std::tuple<uint32_t, uint32_t, PlusGridCandidateKind>>
ToSet(const std::vector<PlusGridCandidateLink>& links)
{
    std::set<std::tuple<uint32_t, uint32_t, PlusGridCandidateKind>> result;
    for (const auto& link : links)
    {
        Check(link.sourceId < link.destinationId, "candidate edge is not canonical");
        Check(result.emplace(link.sourceId, link.destinationId, link.kind).second,
              "candidate edge is duplicated");
    }
    return result;
}

std::vector<SatelliteEcefPosition>
MakeShiftedPlanePositions(uint32_t planes, uint32_t satellitesPerOrbit)
{
    std::vector<SatelliteEcefPosition> positions;
    positions.reserve(planes * satellitesPerOrbit);
    for (uint32_t plane = 0; plane < planes; ++plane)
    {
        for (uint32_t slot = 0; slot < satellitesPerOrbit; ++slot)
        {
            const uint32_t satelliteId = plane * satellitesPerOrbit + slot;
            const uint32_t physicalSlot =
                (slot + satellitesPerOrbit - plane % satellitesPerOrbit) %
                satellitesPerOrbit;
            positions.push_back(
                {satelliteId,
                 Vector(static_cast<double>(physicalSlot * 100),
                        static_cast<double>(plane),
                        0.0)});
        }
    }
    return positions;
}

void
RunCandidateCase()
{
    const ConstellationDefinition config = MakeTestConstellation(3, 4);
    const std::vector<SatelliteEcefPosition> initialPositions =
        MakeShiftedPlanePositions(3, 4);
    const auto candidates = BuildPlusGridCandidateLinks(config, initialPositions);
    Check(candidates.size() == 20, "plus-grid edge count differs");
    for (std::size_t index = 1; index < candidates.size(); ++index)
    {
        const auto previous =
            std::tie(candidates[index - 1].sourceId, candidates[index - 1].destinationId);
        const auto current = std::tie(candidates[index].sourceId,
                                      candidates[index].destinationId);
        Check(previous < current, "plus-grid candidate order is not canonical");
    }

    const auto candidateSet = ToSet(candidates);
    Check(candidateSet.contains({0, 1, PlusGridCandidateKind::INTRA_PLANE}),
          "same-plane ring candidate is missing");
    Check(candidateSet.contains({0, 3, PlusGridCandidateKind::INTRA_PLANE}),
          "same-plane wrap candidate is missing");
    Check(candidateSet.contains({0, 5, PlusGridCandidateKind::INTER_PLANE}),
          "first adjacent-plane initial-nearest match is missing");
    Check(candidateSet.contains({4, 9, PlusGridCandidateKind::INTER_PLANE}),
          "second adjacent-plane initial-nearest match is missing");
    Check(!candidateSet.contains({0, 4, PlusGridCandidateKind::INTER_PLANE}),
          "same-slot inter-plane link replaced the initial-nearest match");
    Check(!candidateSet.contains({0, 8, PlusGridCandidateKind::INTER_PLANE}),
          "final and first orbital planes were connected across the seam");

    const ConstellationDefinition small = MakeTestConstellation(2, 2);
    const std::vector<SatelliteEcefPosition> smallPositions =
        MakeShiftedPlanePositions(2, 2);
    Check(BuildPlusGridCandidateLinks(small, smallPositions).size() == 4,
          "small plus-grid did not deduplicate two-node rings");
    Check(BuildPlusGridCandidateLinks(MakeTestConstellation(1, 1),
                                      MakeShiftedPlanePositions(1, 1))
              .empty(),
          "single-satellite plus-grid created a self-loop");
    Check(candidates == BuildPlusGridCandidateLinks(config, initialPositions),
          "initial-nearest candidate matching is not deterministic");
}

void
RunValidationCase()
{
    ConstellationDefinition invalid = MakeTestConstellation(1, 1);
    invalid.shell.alt = 0.0;
    try
    {
        OnlineOrbitConstellation constellation(invalid);
    }
    catch (const OnlineOrbitConstellationError&)
    {
        Simulator::Destroy();
        return;
    }
    Simulator::Destroy();
    throw std::runtime_error("online orbit accepted an invalid altitude");
}

} // namespace

int
main(int argc, char* argv[])
{
    CommandLine command(__FILE__);
    command.Parse(argc, argv);
    try
    {
        RunIdentityAndMotionCase();
        RunStartOffsetCase();
        RunCandidateCase();
        RunValidationCase();
        std::cout << "SatCompute online orbit foundation tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
