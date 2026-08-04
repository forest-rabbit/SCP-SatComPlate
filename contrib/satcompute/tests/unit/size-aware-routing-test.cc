/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/flow-route-registry.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/satcompute-ipv4-global-routing-helper.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"
#include "ns3/size-aware-hrw-policy.h"
#include "ns3/udp-header.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

constexpr uint16_t DESTINATION_PORT = 9000;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

EcmpFlowKey
MakeFlowKey(uint16_t sourcePort)
{
    EcmpFlowKey key;
    key.sourceAddress = Ipv4Address("172.16.0.1");
    key.destinationAddress = Ipv4Address("172.16.0.4");
    key.protocol = 17;
    key.sourcePort = sourcePort;
    key.destinationPort = DESTINATION_PORT;
    return key;
}

std::vector<EcmpRouteCandidate>
MakeCandidates()
{
    return {
        {Ipv4Address("10.0.0.2"),
         2,
         Ipv4Address("172.16.0.4"),
         Ipv4Mask("255.255.255.255")},
        {Ipv4Address("10.0.0.6"),
         3,
         Ipv4Address("172.16.0.4"),
         Ipv4Mask("255.255.255.255")},
    };
}

void
CheckPolicyAndRegistry()
{
    const std::vector<EcmpRouteCandidate> candidates = MakeCandidates();
    const EcmpFlowKey firstKey = MakeFlowKey(10000);
    const EcmpFlowKey secondKey = MakeFlowKey(10003);
    Ptr<FlowRouteRegistry> registry = CreateObject<FlowRouteRegistry>();
    registry->RegisterTransfer(firstKey, 1, 100);
    registry->RegisterTransfer(secondKey, 2, 200);
    registry->BeginSending(firstKey);
    registry->BeginSending(secondKey);

    SizeAwareHrwPolicy policy(*registry, registry->GetSizeAwareLoadState());
    NextHopDecision first = policy.Select({0, 0, 1, firstKey}, candidates);
    Check(first.candidateIndex == 1 && first.selectionReason == "SIZE_AWARE_HRW_PRIMARY",
          "first size-aware flow did not use its HRW primary");
    Check(registry->GetReservedBytes(0, candidates[1]) == 100,
          "first declared-byte reservation differs");

    NextHopDecision second = policy.Select({0, 0, 1, secondKey}, candidates);
    Check(second.candidateIndex == 0 && second.selectionReason == "SIZE_AWARE_HRW_SECONDARY",
          "second flow did not choose the less-loaded HRW runner-up");
    Check(registry->GetReservedBytes(0, candidates[0]) == 200,
          "second declared-byte reservation differs");
    Check(registry->GetTotalReservedBytes() == 300 && registry->GetAssignmentCount() == 2,
          "size-aware reservation totals differ");

    first = policy.Select({0, 1, 1, firstKey}, candidates);
    Check(first.candidateIndex == 1 && first.selectionReason == "SIZE_AWARE_STICKY",
          "valid size-aware assignment was not sticky");
    Check(registry->GetTotalReservedBytes() == 300,
          "sticky reuse duplicated a reservation");

    const std::vector<EcmpRouteCandidate> onlyFirst = {candidates[0]};
    first = policy.Select({0, 2, 1, firstKey}, onlyFirst);
    Check(first.candidateIndex == 0 &&
              first.selectionReason == "SIZE_AWARE_HRW_PRIMARY",
          "invalid sticky candidate was not deterministically reselected");
    Check(registry->GetReservedBytes(0, candidates[0]) == 300 &&
              registry->GetReservedBytes(0, candidates[1]) == 0,
          "invalid assignment did not move its exact declared-byte reservation");

    const std::vector<FlowRouteReservationEvent>& events = registry->GetEvents();
    Check(events.size() == 5, "size-aware event count before release differs");
    Check(events[0].action == "ASSIGN" &&
              events[0].selectionReason == "SIZE_AWARE_HRW_PRIMARY",
          "primary assignment event differs");
    Check(events[1].action == "ASSIGN" &&
              events[1].selectionReason == "SIZE_AWARE_HRW_SECONDARY",
          "secondary assignment event differs");
    Check(events[2].action == "STICKY_REUSE" &&
              events[2].selectionReason == "SIZE_AWARE_STICKY",
          "sticky event differs");
    Check(events[3].action == "RELEASE_CANDIDATE_INVALID" &&
              events[4].action == "ASSIGN",
          "invalid-candidate release and reassignment order differs");

    registry->FinishSending(firstKey);
    Check(registry->GetTotalReservedBytes() == 200 &&
              registry->GetActiveFlowCount() == 1 &&
              registry->GetAssignmentCount() == 1,
          "first sender release differs");
    registry->FinishSending(secondKey);
    Check(registry->GetTotalReservedBytes() == 0 &&
              registry->GetActiveFlowCount() == 0 &&
              registry->GetAssignmentCount() == 0,
          "final sender release differs");
    Check(registry->GetPeakReservedBytes() == 300 &&
              registry->GetPeakCandidateReservedBytes() == 300,
          "size-aware peak reservation metrics differ");

    registry->Clear();
    Check(registry->GetRegisteredFlowCount() == 0 && registry->GetEvents().empty() &&
              registry->GetPeakReservedBytes() == 0,
          "flow registry clear did not reset all state");
}

Ptr<Ipv4Route>
LookupUdpRoute(Ptr<SatComputeIpv4GlobalRouting> routing, uint16_t sourcePort)
{
    Ptr<Packet> packet = Create<Packet>(32);
    UdpHeader udp;
    udp.SetSourcePort(sourcePort);
    udp.SetDestinationPort(DESTINATION_PORT);
    packet->AddHeader(udp);

    Ipv4Header ipv4;
    ipv4.SetSource(Ipv4Address("172.16.0.1"));
    ipv4.SetDestination(Ipv4Address("172.16.0.4"));
    ipv4.SetProtocol(17);
    ipv4.SetPayloadSize(packet->GetSize());
    Socket::SocketErrno error = Socket::ERROR_NOTERROR;
    Ptr<Ipv4Route> route = routing->RouteOutput(packet, ipv4, nullptr, error);
    Check(error == Socket::ERROR_NOTERROR && route != nullptr,
          "size-aware route lookup failed");
    return route;
}

void
CheckNs348Adapter(const std::string& scenarioFilename)
{
    ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
    config.routing.mode = "global-size-aware-hrw";
    ReplayTopologyController controller(config);
    controller.Initialize();

    Ptr<FlowRouteRegistry> registry = controller.GetFlowRouteRegistry();
    Check(registry != nullptr, "size-aware controller did not create a flow registry");
    Ptr<SatComputeIpv4GlobalRouting> routing = SatComputeIpv4GlobalRoutingHelper::GetRouting(
        controller.GetIdMap().GetNodeBySatelliteId(0));

    Check(LookupUdpRoute(routing, 10001)->GetGateway() == Ipv4Address("10.0.0.2"),
          "unregistered size-aware flow did not use deterministic HRW fallback");
    Check(registry->GetAssignmentCount() == 0,
          "unregistered HRW fallback created a reservation");

    const EcmpFlowKey firstKey = MakeFlowKey(10000);
    const EcmpFlowKey secondKey = MakeFlowKey(10003);
    registry->RegisterTransfer(firstKey, 1, 1000);
    registry->RegisterTransfer(secondKey, 2, 2000);
    registry->BeginSending(firstKey);
    registry->BeginSending(secondKey);

    Check(LookupUdpRoute(routing, 10000)->GetGateway() == Ipv4Address("10.0.0.6"),
          "first adapter flow did not use its HRW primary");
    Check(LookupUdpRoute(routing, 10003)->GetGateway() == Ipv4Address("10.0.0.2"),
          "second adapter flow did not use the less-loaded runner-up");
    Check(registry->GetTotalReservedBytes() == 3000,
          "adapter declared-byte reservation total differs");

    bool removalChecked = false;
    Simulator::Schedule(Seconds(3), [&routing, &registry, &removalChecked] {
        Check(routing->GetRouteEpoch() == 1,
              "size-aware route epoch did not advance at edge removal");
        Check(LookupUdpRoute(routing, 10000)->GetGateway() == Ipv4Address("10.0.0.6"),
              "valid sticky flow moved after an unrelated candidate removal");
        Check(LookupUdpRoute(routing, 10003)->GetGateway() == Ipv4Address("10.0.0.6"),
              "invalid sticky flow was not reselected");
        Check(registry->GetReservedBytes(0, MakeCandidates()[1]) == 3000,
              "reselected adapter reservations differ");
        removalChecked = true;
    });
    Simulator::Stop(NanoSeconds(config.simulation.durationNs));
    Simulator::Run();

    Check(removalChecked, "size-aware dynamic checkpoint did not run");
    Check(controller.GetRouteComputationCount() == 3 && routing->GetRouteEpoch() == 2,
          "size-aware dynamic route updates differ");
    Check(LookupUdpRoute(routing, 10000)->GetGateway() == Ipv4Address("10.0.0.6") &&
              LookupUdpRoute(routing, 10003)->GetGateway() == Ipv4Address("10.0.0.6"),
          "restored candidate set broke sticky size-aware assignments");

    registry->FinishSending(firstKey);
    registry->FinishSending(secondKey);
    Check(registry->GetTotalReservedBytes() == 0 && registry->GetAssignmentCount() == 0,
          "adapter sender completion leaked reservations");
    Check(registry->GetPeakReservedBytes() == 3000 &&
              registry->GetPeakCandidateReservedBytes() == 3000,
          "adapter peak reservations differ");
    Simulator::Destroy();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenarioFilename;
    CommandLine command(__FILE__);
    command.AddValue("scenario", "Dynamic diamond scenario", scenarioFilename);
    command.Parse(argc, argv);

    try
    {
        Check(!scenarioFilename.empty(), "scenario is required");
        CheckPolicyAndRegistry();
        CheckNs348Adapter(scenarioFilename);
        std::cout << "SatCompute IPv4 size-aware routing tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
