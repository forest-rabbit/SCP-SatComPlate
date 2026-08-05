/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/channel.h"
#include "ns3/command-line.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/simulator.h"

#include "../support/config-factory.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeOnlineTestConfig;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void
IncrementCounter(uint32_t* counter)
{
    ++*counter;
}

void
ResetSimulationGlobals()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

int64_t
GetLinkDelayNs(const OnlineTopologyController& controller,
               uint32_t first,
               uint32_t second)
{
    const NetDeviceContainer devices = controller.GetLinkState().GetLinkDevices(first, second);
    const Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(devices.Get(0));
    Check(device != nullptr, "online ISL device type differs");
    TimeValue delay;
    device->GetChannel()->GetAttribute("Delay", delay);
    return delay.Get().GetNanoSeconds();
}

void
RunPolicyContractCase()
{
    ResolvedSatComputeConfig config =
        MakeOnlineTestConfig(1, 2, "distance", 1000000000, 1000000000, 5.0L);
    const std::vector<SatelliteEcefPosition> positions = {
        {0, Vector(0.0, 0.0, 0.0)},
        {1, Vector(3.0, 4.0, 0.0)},
    };
    const CircularOrbitTopologyPolicy boundaryPolicy(config);
    const CircularOrbitTopologyState boundary = boundaryPolicy.EvaluatePositions(0, positions);
    Check(boundary.evaluatedLinks.size() == 1,
          "two-satellite policy candidate count differs");
    Check(boundary.evaluatedLinks.front().active,
          "candidate at exactly max_isl_distance_m was disabled");
    Check(boundary.evaluatedLinks.front().distanceM == 5.0,
          "raw Cartesian candidate distance differs");
    Check(boundary.evaluatedLinks.front().delayNs == 17,
          "distance propagation delay was not rounded to nearest nanosecond");
    Check(DistanceToPropagationDelayNs(299792.458) == 1000000,
          "one-way speed-of-light conversion differs");

    config.network.maxIslDistanceM = std::nextafter(5.0L, 0.0L);
    const CircularOrbitTopologyPolicy belowBoundaryPolicy(config);
    Check(!belowBoundaryPolicy.EvaluatePositions(0, positions)
               .evaluatedLinks.front()
               .active,
          "candidate above max_isl_distance_m remained active");
}

void
RunControllerValidationCase()
{
    const ResolvedSatComputeConfig excessive =
        MakeOnlineTestConfig(1,
                             2,
                             "fixed",
                             std::numeric_limits<int64_t>::max(),
                             1,
                             30000000.0L);
    try
    {
        OnlineTopologyController controller(excessive);
    }
    catch (const OnlineTopologyControllerError&)
    {
        return;
    }
    throw std::runtime_error("online controller accepted an overflowing update count");
}

void
RunFixedPeriodicCase()
{
    const ResolvedSatComputeConfig config =
        MakeOnlineTestConfig(2, 3, "fixed", 2500000000LL, 1000000000LL, 30000000.0L);
    {
        OnlineTopologyController controller(config);
        controller.Initialize();
        Check(controller.GetAppliedUpdateCount() == 1,
              "online initial evaluation count differs");
        Check(controller.GetRouteComputationCount() == 1,
              "online initial route count differs");
        Check(controller.GetLinkState().GetActiveLinks().size() == 9,
              "fixed online plus-grid did not activate all candidates");
        Check(controller.GetServiceAddress(0) == Ipv4Address("172.16.0.1"),
              "online service address differs from replay");
        Check(GetLinkDelayNs(controller, 0, 1) == 8000000,
              "fixed online delay differs at time zero");

        uint32_t callbackCount = 0;
        controller.RegisterRouteUpdateCallback(
            MakeBoundCallback(&IncrementCounter, &callbackCount));
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(controller.GetAppliedUpdateCount() == 3,
              "online controller did not evaluate exactly at 0, 1, and 2 seconds");
        Check(controller.GetAppliedUpdateTimesNs() ==
                  std::vector<int64_t>({0, 1000000000LL, 2000000000LL}),
              "online network update timestamps differ");
        Check(controller.GetRouteComputationCount() == 1 && callbackCount == 0,
              "unchanged fixed topology rebuilt routes");
        Check(!controller.GetLastUpdateSummary().ActiveEdgeSetChanged(),
              "unchanged fixed topology reported an edge change");
        Check(GetLinkDelayNs(controller, 0, 1) == 8000000,
              "fixed delay changed between update points");
    }
    ResetSimulationGlobals();
}

void
RunDistanceDelayOnlyCase()
{
    const ResolvedSatComputeConfig config = MakeOnlineTestConfig(
        3,
        4,
        "distance",
        21000000000LL,
        10000000000LL,
        30000000.0L);
    {
        OnlineTopologyController controller(config);
        controller.Initialize();
        const std::vector<EvaluatedSatelliteLink> initial =
            controller.GetLastTopologyState().evaluatedLinks;

        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(controller.GetAppliedUpdateCount() == 3,
              "distance controller update count differs");
        Check(controller.GetRouteComputationCount() == 1,
              "distance-only delay changes rebuilt routes");
        Check(!controller.GetLastUpdateSummary().ActiveEdgeSetChanged(),
              "distance-only update changed the edge set");
        Check(controller.GetLastUpdateSummary().reconfiguredLinks > 0,
              "distance mode did not refresh active channel delays");

        bool delayChanged = false;
        const auto& finalLinks = controller.GetLastTopologyState().evaluatedLinks;
        for (std::size_t index = 0; index < initial.size(); ++index)
        {
            delayChanged = delayChanged || initial[index].delayNs != finalLinks[index].delayNs;
        }
        Check(delayChanged, "distance mode produced no changed propagation delay");
    }
    ResetSimulationGlobals();
}

long double
FindCrossingThreshold()
{
    const ResolvedSatComputeConfig config = MakeOnlineTestConfig(
        3,
        4,
        "distance",
        61000000000LL,
        60000000000LL,
        30000000.0L);
    CircularOrbitTopologyState initial;
    CircularOrbitTopologyState finalState;
    {
        OnlineOrbitConstellation constellation(config.constellation);
        CircularOrbitTopologyPolicy policy(config);
        initial = policy.EvaluateCurrent(constellation);
        Simulator::Schedule(Seconds(60), [&] {
            finalState = policy.EvaluateCurrent(constellation);
        });
        Simulator::Stop(Seconds(60));
        Simulator::Run();
    }
    ResetSimulationGlobals();

    for (std::size_t index = 0; index < initial.evaluatedLinks.size(); ++index)
    {
        const double first = initial.evaluatedLinks[index].distanceM;
        const double second = finalState.evaluatedLinks[index].distanceM;
        if (std::abs(first - second) > 1.0)
        {
            return (static_cast<long double>(first) + second) / 2.0L;
        }
    }
    throw std::runtime_error("test constellation has no changing candidate distance");
}

void
RunEdgeChangeCase()
{
    const long double threshold = FindCrossingThreshold();
    const ResolvedSatComputeConfig config = MakeOnlineTestConfig(
        3,
        4,
        "fixed",
        61000000000LL,
        60000000000LL,
        threshold);
    {
        OnlineTopologyController controller(config);
        controller.Initialize();
        const auto initialEdges = controller.GetLinkState().GetActiveLinks();
        uint32_t callbackCount = 0;
        controller.RegisterRouteUpdateCallback(
            MakeBoundCallback(&IncrementCounter, &callbackCount));

        bool updatePrecededSameTimeObserver = false;
        Simulator::Schedule(Seconds(60), [&] {
            updatePrecededSameTimeObserver = controller.GetAppliedUpdateCount() == 2;
        });
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(controller.GetLinkState().GetActiveLinks() != initialEdges,
              "engineered distance crossing did not change active edges");
        Check(controller.GetAppliedUpdateCount() == 2,
              "edge-change controller update count differs");
        Check(controller.GetRouteComputationCount() == 2 && callbackCount == 1,
              "one atomic edge-change tick did not rebuild routes exactly once");
        Check(controller.GetLastUpdateSummary().ActiveEdgeSetChanged(),
              "edge crossing was not reported by link state");
        Check(updatePrecededSameTimeObserver,
              "same-time event observed the network before its periodic update");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    CommandLine command(__FILE__);
    command.Parse(argc, argv);
    try
    {
        RunPolicyContractCase();
        RunControllerValidationCase();
        RunFixedPeriodicCase();
        RunDistanceDelayOnlyCase();
        RunEdgeChangeCase();
        std::cout << "SatCompute online topology controller tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
