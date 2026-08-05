/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/ecmp-flow-key.h"
#include "ns3/ecmp-route-candidate.h"
#include "ns3/fnv1a64.h"
#include "ns3/hash-per-flow-policy.h"
#include "ns3/hrw-per-flow-policy.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/routing-mode.h"
#include "ns3/satcompute-ipv4-global-routing-helper.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"

#include "../support/config-factory.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeDiamondReplayTestConfig;

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
MakeDiamondCandidates()
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
CheckRoutingModeNames()
{
    const std::array<std::pair<const char*, RoutingMode>, 5> modes = {{
        {"global-first", RoutingMode::GLOBAL_FIRST},
        {"global-hash-per-flow", RoutingMode::HASH_PER_FLOW},
        {"global-hrw-per-flow", RoutingMode::HRW_PER_FLOW},
        {"global-size-aware-hrw", RoutingMode::SIZE_AWARE_HRW},
        {"global-capacity-aware-hrw", RoutingMode::CAPACITY_AWARE_HRW},
    }};
    for (const auto& [name, expected] : modes)
    {
        RoutingMode parsed = RoutingMode::GLOBAL_FIRST;
        Check(TryParseRoutingMode(name, parsed), "routing mode did not parse");
        Check(parsed == expected, "routing mode enum differs");
        Check(std::string(GetRoutingModeName(parsed)) == name,
              "routing mode name did not round trip");
    }
    RoutingMode ignored = RoutingMode::GLOBAL_FIRST;
    Check(!TryParseRoutingMode("random-ecmp", ignored), "unknown routing mode parsed successfully");
}

void
CheckLegacyHashGolden()
{
    const std::array<uint8_t, 21> expectedEncoding = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xac, 0x10, 0x00,
        0x01, 0xac, 0x10, 0x00, 0x04, 0x11, 0x27, 0x10, 0x23, 0x28,
    };
    Check(EncodeEcmpFlowKey(1, MakeFlowKey(10000)) == expectedEncoding,
          "legacy 21-byte flow-key encoding changed");

    const std::array<uint64_t, 4> expectedHashes = {
        689561907430475796ULL,
        9897695115818735ULL,
        1941789103400206030ULL,
        1315710689787443665ULL,
    };
    const std::vector<EcmpRouteCandidate> candidates = MakeDiamondCandidates();

    HashPerFlowPolicy policy;
    for (uint16_t offset = 0; offset < expectedHashes.size(); ++offset)
    {
        const EcmpFlowKey key = MakeFlowKey(10000 + offset);
        const uint64_t hash = Fnv1a64(EncodeEcmpFlowKey(1, key));
        Check(hash == expectedHashes[offset], "legacy FNV-1a-64 hash changed");
        const NextHopSelectionContext context = {0, 0, 1, key};
        const NextHopDecision decision = policy.Select(context, candidates);
        Check(decision.score == hash, "hash policy score differs");
        Check(decision.candidateIndex == hash % candidates.size(), "hash policy candidate differs");
        Check(decision.selectionReason == "HASH_PER_FLOW", "hash policy reason differs");
    }
}

void
CheckLegacyHrwGolden()
{
    const std::vector<EcmpRouteCandidate> candidates = MakeDiamondCandidates();
    const std::array<uint8_t, 37> expectedEncoding = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xac, 0x10,
        0x00, 0x01, 0xac, 0x10, 0x00, 0x04, 0x11, 0x27, 0x10, 0x23,
        0x28, 0x0a, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0xac,
        0x10, 0x00, 0x04, 0xff, 0xff, 0xff, 0xff,
    };
    Check(EncodeEcmpHrwKey(1, MakeFlowKey(10000), candidates[0]) == expectedEncoding,
          "legacy 37-byte HRW encoding changed");

    const std::array<std::array<uint64_t, 2>, 4> expectedScores = {{
        {8335418045070543154ULL, 17202181822749059529ULL},
        {12373607477971096641ULL, 10727782909931175850ULL},
        {9698437838575336000ULL, 3862722980943077063ULL},
        {924498406868803895ULL, 6049471179029509744ULL},
    }};
    const std::array<uint32_t, 4> expectedIndices = {1, 0, 0, 1};
    HrwPerFlowPolicy policy;
    for (uint16_t offset = 0; offset < expectedScores.size(); ++offset)
    {
        const EcmpFlowKey key = MakeFlowKey(10000 + offset);
        for (uint32_t candidate = 0; candidate < candidates.size(); ++candidate)
        {
            Check(ScoreEcmpHrwRoute(1, key, candidates[candidate]) ==
                      expectedScores[offset][candidate],
                  "legacy HRW score changed");
        }
        const NextHopSelectionContext context = {0, 0, 1, key};
        const NextHopDecision decision = policy.Select(context, candidates);
        Check(decision.candidateIndex == expectedIndices[offset],
              "legacy HRW candidate changed");
        Check(decision.score == expectedScores[offset][expectedIndices[offset]],
              "legacy HRW winning score changed");
        Check(decision.selectionReason == "HRW_PER_FLOW", "HRW policy reason differs");
    }
}

void
CheckHrwMinimalDisruption()
{
    std::vector<EcmpRouteCandidate> candidates = MakeDiamondCandidates();
    candidates.push_back({Ipv4Address("10.0.0.10"),
                          4,
                          Ipv4Address("172.16.0.4"),
                          Ipv4Mask("255.255.255.255")});
    const EcmpFlowKey key = MakeFlowKey(10005);
    const std::vector<EcmpHrwRank> ranking = RankEcmpHrwRoutes(1, key, candidates);
    Check(ranking.size() == candidates.size(), "HRW ranking size differs");
    const EcmpRouteCandidate winner = candidates[ranking[0].candidateIndex];
    const EcmpRouteCandidate runnerUp = candidates[ranking[1].candidateIndex];

    std::vector<EcmpRouteCandidate> reordered = {candidates[2], candidates[0], candidates[1]};
    EcmpHrwSelection selection = SelectEcmpHrwRoute(1, key, reordered);
    Check(reordered[selection.candidateIndex] == winner,
          "HRW selection depends on candidate order");

    std::vector<EcmpRouteCandidate> withoutUnselected = candidates;
    withoutUnselected.erase(withoutUnselected.begin() + ranking.back().candidateIndex);
    selection = SelectEcmpHrwRoute(1, key, withoutUnselected);
    Check(withoutUnselected[selection.candidateIndex] == winner,
          "removing an unselected HRW route moved the flow");

    std::vector<EcmpRouteCandidate> withoutWinner;
    for (const auto& candidate : candidates)
    {
        if (!(candidate == winner))
        {
            withoutWinner.push_back(candidate);
        }
    }
    selection = SelectEcmpHrwRoute(1, key, withoutWinner);
    Check(withoutWinner[selection.candidateIndex] == runnerUp,
          "removing the HRW winner did not select the previous runner-up");
    Check(ScoreEcmpHrwRoute(2, key, winner) != ScoreEcmpHrwRoute(1, key, winner),
          "HRW seed does not affect the score");
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
    Check(error == Socket::ERROR_NOTERROR, "per-flow route lookup set socket error");
    Check(route != nullptr, "per-flow route lookup returned no route");
    return route;
}

void
CheckNs348Adapter(const std::filesystem::path& topologyDirectory)
{
    ResolvedSatComputeConfig config = MakeDiamondReplayTestConfig(topologyDirectory);
    config.routing.mode = "global-hash-per-flow";
    ReplayTopologyController controller(config);
    controller.Initialize();

    Ptr<SatComputeIpv4GlobalRouting> routing = SatComputeIpv4GlobalRoutingHelper::GetRouting(
        controller.GetIdMap().GetNodeBySatelliteId(0));
    Check(routing->GetRouteEpoch() == 0, "initial route epoch differs");

    const std::vector<EcmpRouteCandidate> candidates =
        routing->GetEffectiveRouteCandidates(controller.GetServiceAddress(3));
    Check(candidates.size() == 2, "diamond topology does not expose two ECMP routes");
    Check(candidates[0].gateway == Ipv4Address("10.0.0.2") && candidates[0].outputInterface == 2,
          "first canonical ECMP route differs");
    Check(candidates[1].gateway == Ipv4Address("10.0.0.6") && candidates[1].outputInterface == 3,
          "second canonical ECMP route differs");

    const std::array<Ipv4Address, 4> expectedGateways = {
        Ipv4Address("10.0.0.2"),
        Ipv4Address("10.0.0.6"),
        Ipv4Address("10.0.0.2"),
        Ipv4Address("10.0.0.6"),
    };
    for (uint16_t offset = 0; offset < expectedGateways.size(); ++offset)
    {
        Check(LookupUdpRoute(routing, 10000 + offset)->GetGateway() == expectedGateways[offset],
              "ns-3.48 adapter changed a legacy hash selection");
    }

    Simulator::Stop(NanoSeconds(config.simulation.durationNs));
    Simulator::Run();
    Check(controller.GetRouteComputationCount() == 3,
          "dynamic diamond route computation count differs");
    Check(routing->GetRouteEpoch() == 2, "route epoch did not advance once per active-edge change");
    Check(LookupUdpRoute(routing, 10000)->GetGateway() == expectedGateways[0],
          "restored candidate set changed deterministic hash selection");
    Simulator::Destroy();
}

void
CheckNs348HrwAdapter(const std::filesystem::path& topologyDirectory)
{
    ResolvedSatComputeConfig config = MakeDiamondReplayTestConfig(topologyDirectory);
    config.routing.mode = "global-hrw-per-flow";
    ReplayTopologyController controller(config);
    controller.Initialize();

    Ptr<SatComputeIpv4GlobalRouting> routing = SatComputeIpv4GlobalRoutingHelper::GetRouting(
        controller.GetIdMap().GetNodeBySatelliteId(0));
    const std::array<Ipv4Address, 2> initialGateways = {
        Ipv4Address("10.0.0.6"),
        Ipv4Address("10.0.0.2"),
    };
    Check(LookupUdpRoute(routing, 10000)->GetGateway() == initialGateways[0],
          "ns-3.48 adapter changed the first legacy HRW selection");
    Check(LookupUdpRoute(routing, 10001)->GetGateway() == initialGateways[1],
          "ns-3.48 adapter changed the second legacy HRW selection");

    bool intermediateChecked = false;
    Simulator::Schedule(Seconds(3), [&routing, &intermediateChecked] {
        Check(routing->GetRouteEpoch() == 1, "HRW route epoch did not advance at edge removal");
        Check(LookupUdpRoute(routing, 10000)->GetGateway() == Ipv4Address("10.0.0.6"),
              "removing an unselected route moved an HRW flow");
        Check(LookupUdpRoute(routing, 10001)->GetGateway() == Ipv4Address("10.0.0.6"),
              "removing the selected route did not remap the HRW flow");
        intermediateChecked = true;
    });
    Simulator::Stop(NanoSeconds(config.simulation.durationNs));
    Simulator::Run();

    Check(intermediateChecked, "HRW dynamic-topology checkpoint did not run");
    Check(controller.GetRouteComputationCount() == 3,
          "HRW dynamic route computation count differs");
    Check(routing->GetRouteEpoch() == 2, "HRW route epoch did not advance at edge restoration");
    Check(LookupUdpRoute(routing, 10000)->GetGateway() == initialGateways[0],
          "restored candidates did not restore the first HRW selection");
    Check(LookupUdpRoute(routing, 10001)->GetGateway() == initialGateways[1],
          "restored candidates did not restore the second HRW selection");
    Simulator::Destroy();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string topologyDirectory;
    CommandLine command(__FILE__);
    command.AddValue("topologyDir", "Dynamic diamond topology slices", topologyDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!topologyDirectory.empty(), "topologyDir is required");
        CheckRoutingModeNames();
        CheckLegacyHashGolden();
        CheckLegacyHrwGolden();
        CheckHrwMinimalDisruption();
        CheckNs348Adapter(topologyDirectory);
        CheckNs348HrwAdapter(topologyDirectory);
        std::cout << "SatCompute IPv4 first/hash/HRW routing tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
