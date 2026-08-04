/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/geographic-positions.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/plus-grid-candidate.h"
#include "ns3/simulator.h"

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

using namespace ns3;

namespace
{

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

ConstellationConfig
MakeConfig(uint32_t numOrbits,
           uint32_t satellitesPerOrbit,
           const std::string& pattern = "walker-star",
           bool phaseDiff = true,
           int64_t epochOffsetNs = 0)
{
    ConstellationConfig config{};
    config.orbitProvider = "ns3-circular";
    config.constellationName = "unit-test";
    config.constellationPattern = pattern;
    config.numOrbits = numOrbits;
    config.satellitesPerOrbit = satellitesPerOrbit;
    config.altitudeM = 780000.0L;
    config.inclinationDeg = 86.4L;
    config.phaseDiff = phaseDiff;
    config.orbitEpochOffsetNs = epochOffsetNs;
    return config;
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
        OnlineOrbitConstellation constellation(MakeConfig(3, 4));
        const auto& identities = constellation.GetOrbitIdentities();
        Check(constellation.GetNodes().GetN() == 12, "online orbit node count differs");
        Check(identities.size() == 12, "online orbit identity count differs");
        Check(identities.at(6).satelliteId == 6 && identities.at(6).planeIndex == 1 &&
                  identities.at(6).slotIndex == 2,
              "plane-major satellite identity differs");
        CheckClose(identities.at(4).raanDeg, 60.0, 1e-12, "Walker Star RAAN differs");
        CheckClose(identities.at(8).raanDeg, 120.0, 1e-12, "Walker Star span differs");
        CheckClose(identities.at(4).baseArgumentLatitudeDeg,
                   45.0,
                   1e-12,
                   "odd-plane half-slot phase differs");
        CheckClose(identities.at(6).baseArgumentLatitudeDeg,
                   225.0,
                   1e-12,
                   "slot argument latitude differs");
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

        double movementM = 0.0;
        Simulator::Schedule(Seconds(1), [&] {
            movementM = Distance(initial, constellation.GetPosition(0));
        });
        Simulator::Stop(Seconds(1));
        Simulator::Run();
        Check(movementM > 1000.0, "official mobility position did not evolve continuously");
    }
    Simulator::Destroy();
}

void
RunPatternCase()
{
    {
        OnlineOrbitConstellation delta(MakeConfig(3, 4, "walker-delta", false));
        const auto& identities = delta.GetOrbitIdentities();
        CheckClose(identities.at(4).raanDeg, 120.0, 1e-12, "Walker Delta RAAN differs");
        CheckClose(identities.at(8).raanDeg, 240.0, 1e-12, "Walker Delta span differs");
        CheckClose(identities.at(4).baseArgumentLatitudeDeg,
                   0.0,
                   1e-12,
                   "disabled phase_diff changed an odd plane");
    }
    Simulator::Destroy();
}

void
RunEpochOffsetCase()
{
    constexpr int64_t offsetNs = 100000000000LL;
    {
        OnlineOrbitConstellation offset(MakeConfig(2, 3, "walker-star", true, offsetNs));
        OnlineOrbitConstellation reference(MakeConfig(2, 3));
        const Vector offsetPositionAtZero = offset.GetPosition(4);
        Vector referencePositionAtOffset;
        Simulator::Schedule(NanoSeconds(offsetNs), [&] {
            referencePositionAtOffset = reference.GetPosition(4);
        });
        Simulator::Stop(NanoSeconds(offsetNs));
        Simulator::Run();
        Check(Distance(offsetPositionAtZero, referencePositionAtOffset) < 0.001,
              "orbit_epoch_offset did not include orbital progress and Earth rotation");
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

void
RunCandidateCase()
{
    const ConstellationConfig config = MakeConfig(3, 4);
    const auto withoutSeam = BuildPlusGridCandidateLinks(config, false);
    const auto withSeam = BuildPlusGridCandidateLinks(config, true);
    Check(withoutSeam.size() == 20, "plus-grid no-seam edge count differs");
    Check(withSeam.size() == 24, "plus-grid seam edge count differs");
    for (std::size_t index = 1; index < withSeam.size(); ++index)
    {
        const auto previous =
            std::tie(withSeam[index - 1].sourceId, withSeam[index - 1].destinationId);
        const auto current = std::tie(withSeam[index].sourceId, withSeam[index].destinationId);
        Check(previous < current, "plus-grid candidate order is not canonical");
    }

    const auto noSeamSet = ToSet(withoutSeam);
    const auto seamSet = ToSet(withSeam);
    Check(noSeamSet.contains({0, 1, PlusGridCandidateKind::INTRA_PLANE}),
          "same-plane ring candidate is missing");
    Check(noSeamSet.contains({0, 3, PlusGridCandidateKind::INTRA_PLANE}),
          "same-plane wrap candidate is missing");
    Check(noSeamSet.contains({0, 4, PlusGridCandidateKind::INTER_PLANE}),
          "adjacent-plane candidate is missing");
    Check(!noSeamSet.contains({0, 8, PlusGridCandidateKind::INTER_PLANE}),
          "disabled seam created a final-to-first-plane candidate");
    Check(seamSet.contains({0, 8, PlusGridCandidateKind::INTER_PLANE}),
          "enabled seam candidate is missing");

    const ConstellationConfig small = MakeConfig(2, 2);
    Check(BuildPlusGridCandidateLinks(small, false).size() == 4,
          "small plus-grid did not deduplicate two-node rings");
    Check(BuildPlusGridCandidateLinks(small, true).size() == 4,
          "small plus-grid did not deduplicate its seam");
    Check(BuildPlusGridCandidateLinks(MakeConfig(1, 1), true).empty(),
          "single-satellite plus-grid created a self-loop");
    Check(withoutSeam == BuildPlusGridCandidateLinks(config, false),
          "fixed candidate identities changed between evaluations");
}

void
RunValidationCase()
{
    ConstellationConfig invalid = MakeConfig(1, 1);
    invalid.orbitProvider = "json-replay";
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
    throw std::runtime_error("online orbit accepted a replay provider");
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
        RunPatternCase();
        RunEpochOffsetCase();
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
