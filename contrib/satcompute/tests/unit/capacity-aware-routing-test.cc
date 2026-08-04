/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/capacity-aware-hrw-policy.h"
#include "ns3/capacity-reservation-state.h"
#include "ns3/command-line.h"
#include "ns3/flow-route-registry.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/satcompute-ipv4-global-routing-helper.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"
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
constexpr uint64_t LINK_RATE_BPS = 100000000;

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

PathSelectionContext
MakePathContext(const EcmpFlowKey& flowKey, uint64_t hashSeed)
{
    return {flowKey, 0, 3, hashSeed};
}

std::vector<uint32_t>
GetPathSatelliteIds(const CapacityAwarePath& path)
{
    std::vector<uint32_t> ids;
    Check(!path.hops.empty(), "capacity-aware path is empty");
    ids.push_back(path.hops.front().sourceSatelliteId);
    for (const CapacityAwarePathHop& hop : path.hops)
    {
        ids.push_back(hop.destinationSatelliteId);
    }
    return ids;
}

void
RecordPathAssignments(ReplayTopologyController& controller,
                      Ptr<FlowRouteRegistry> registry,
                      const EcmpFlowKey& flowKey,
                      const CapacityAwarePath& path)
{
    for (const CapacityAwarePathHop& hop : path.hops)
    {
        // A degree-one router may expose an unambiguous default route. Only
        // host-route candidates participate in the per-flow adapter.
        if (hop.candidate.destinationMask != Ipv4Mask("255.255.255.255"))
        {
            continue;
        }
        registry->RecordAssignment(hop.sourceSatelliteId,
                                   flowKey,
                                   hop.candidate,
                                   controller.GetRouteEpoch(hop.sourceSatelliteId),
                                   "CAPACITY_AWARE_PATH");
    }
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
          "capacity-aware route lookup failed");
    return route;
}

void
CheckParallelAdmission(ReplayTopologyController& controller)
{
    CapacityReservationState state;
    CapacityAwareHrwPolicy policy(controller, state);
    const EcmpFlowKey firstKey = MakeFlowKey(10001);
    const EcmpFlowKey secondKey = MakeFlowKey(10000);
    const EcmpFlowKey waitingKey = MakeFlowKey(10002);

    CapacityAwarePath first;
    CapacityAwarePath repeated;
    Check(policy.FindPath(MakePathContext(firstKey, controller.GetHashSeed()), first),
          "first complete path was not admitted");
    Check(policy.FindPath(MakePathContext(firstKey, controller.GetHashSeed()), repeated),
          "same-epoch repeated path lookup failed");
    Check(GetPathSatelliteIds(first) == GetPathSatelliteIds(repeated),
          "same-epoch path selection was not deterministic");
    Check(first.hops.size() == 2 && first.admittedRateBps == LINK_RATE_BPS,
          "first path bottleneck rate differs");
    state.Reserve(1, first);
    Check(state.IsActivePathValid(1, 3, controller), "first reserved path is invalid");

    CapacityAwarePath second;
    Check(policy.FindPath(MakePathContext(secondKey, controller.GetHashSeed()), second),
          "parallel path was not admitted");
    Check(first.hops.front().destinationSatelliteId !=
              second.hops.front().destinationSatelliteId,
          "parallel admission reused a full first hop");
    Check(second.admittedRateBps == LINK_RATE_BPS,
          "parallel path bottleneck rate differs");
    state.Reserve(2, second);

    CapacityAwarePath waiting;
    Check(!policy.FindPath(MakePathContext(waitingKey, controller.GetHashSeed()), waiting),
          "fully reserved topology admitted an extra path");
    Check(waiting.hops.empty() && waiting.admittedRateBps == 0,
          "failed admission returned stale path state");

    const CapacityAwareRuntimeSummary full = state.CollectSummary();
    Check(full.activePathCountAtEnd == 2 && full.reservedDirectedLinkCountAtEnd == 4 &&
              full.totalReservedRateBpsAtEnd == 4 * LINK_RATE_BPS,
          "parallel reservation summary differs");

    state.Release(1);
    Check(policy.FindPath(MakePathContext(waitingKey, controller.GetHashSeed()), waiting),
          "waiting flow was not admitted after release");
    Check(waiting.admittedRateBps == LINK_RATE_BPS &&
              waiting.hops.front().destinationSatelliteId ==
                  first.hops.front().destinationSatelliteId,
          "released capacity was not reused deterministically");
    state.Reserve(3, waiting);
    state.Release(2);
    state.Release(3);
    const CapacityAwareRuntimeSummary empty = state.CollectSummary();
    Check(empty.activePathCountAtEnd == 0 &&
              empty.reservedDirectedLinkCountAtEnd == 0 &&
              empty.totalReservedRateBpsAtEnd == 0,
          "capacity reservations did not return to zero");
}

class RecoveryHarness
{
  public:
    RecoveryHarness(ReplayTopologyController& controller,
                    Ptr<FlowRouteRegistry> registry,
                    CapacityAwareHrwPolicy& policy,
                    CapacityReservationState& state,
                    EcmpFlowKey flowKey,
                    uint64_t transferId)
        : m_controller(controller),
          m_registry(registry),
          m_policy(policy),
          m_state(state),
          m_flowKey(flowKey),
          m_transferId(transferId)
    {
    }

    void
    AdmitInitialPath()
    {
        Check(m_policy.FindPath(MakePathContext(m_flowKey, m_controller.GetHashSeed()),
                                m_path),
              "initial recovery path was not admitted");
        RecordPathAssignments(m_controller, m_registry, m_flowKey, m_path);
        m_state.Reserve(m_transferId, m_path);
    }

    void
    HandleRouteUpdate()
    {
        ++routeUpdateCount;
        if (m_state.IsActivePathValid(m_transferId, 3, m_controller))
        {
            return;
        }

        m_registry->ReleaseAssignmentsForRouteUpdate(m_flowKey,
                                                      m_controller.GetRouteEpoch(0));
        m_state.Release(m_transferId);
        CapacityAwarePath replacement;
        Check(m_policy.FindPath(MakePathContext(m_flowKey, m_controller.GetHashSeed()),
                                replacement),
              "invalid complete path could not be re-admitted");
        RecordPathAssignments(m_controller, m_registry, m_flowKey, replacement);
        m_state.Reserve(m_transferId, replacement);
        m_controller.InvalidateFlowRouteDecisionCache(m_flowKey);
        m_path = replacement;
        ++readmissionCount;
    }

    const CapacityAwarePath&
    GetPath() const
    {
        return m_path;
    }

    uint32_t routeUpdateCount{};
    uint32_t readmissionCount{};

  private:
    ReplayTopologyController& m_controller;
    Ptr<FlowRouteRegistry> m_registry;
    CapacityAwareHrwPolicy& m_policy;
    CapacityReservationState& m_state;
    EcmpFlowKey m_flowKey;
    uint64_t m_transferId;
    CapacityAwarePath m_path;
};

void
CheckDynamicRecovery(ReplayTopologyController& controller)
{
    Ptr<FlowRouteRegistry> registry = controller.GetFlowRouteRegistry();
    Check(registry != nullptr && controller.IsCapacityAwareRouting(),
          "capacity-aware controller state is missing");
    const EcmpFlowKey flowKey = MakeFlowKey(10001);
    registry->RegisterTransfer(flowKey, 10, 4096);
    registry->BeginSending(flowKey);

    CapacityReservationState state;
    CapacityAwareHrwPolicy policy(controller, state);
    RecoveryHarness harness(controller, registry, policy, state, flowKey, 10);
    harness.AdmitInitialPath();
    Check(GetPathSatelliteIds(harness.GetPath()) == std::vector<uint32_t>({0, 1, 3}),
          "initial recovery path differs from the HRW golden");

    Ptr<SatComputeIpv4GlobalRouting> routing = SatComputeIpv4GlobalRoutingHelper::GetRouting(
        controller.GetIdMap().GetNodeBySatelliteId(0));
    Check(LookupUdpRoute(routing, 10001)->GetGateway() == Ipv4Address("10.0.0.2"),
          "capacity-aware adapter ignored the admitted complete path");

    controller.RegisterRouteUpdateCallback(
        MakeCallback(&RecoveryHarness::HandleRouteUpdate, &harness));
    Simulator::Stop(NanoSeconds(controller.GetConfig().simulation.durationNs));
    Simulator::Run();

    Check(harness.routeUpdateCount == 2 && harness.readmissionCount == 1,
          "route-update callback or complete-path re-admission count differs");
    Check(GetPathSatelliteIds(harness.GetPath()) == std::vector<uint32_t>({0, 2, 3}),
          "recovered complete path differs");
    Check(state.IsActivePathValid(10, 3, controller),
          "recovered complete path is not valid after topology restoration");
    Check(LookupUdpRoute(routing, 10001)->GetGateway() == Ipv4Address("10.0.0.6"),
          "capacity-aware adapter did not follow the recovered path");

    registry->FinishReceiving(flowKey);
    state.Release(10);
    Check(registry->GetAssignmentCount() == 0 &&
              registry->GetTotalReservedBytes() == 0 &&
              state.CollectSummary().activePathCountAtEnd == 0,
          "capacity-aware completion leaked route or capacity reservations");
}

void
RunCapacityAwareCases(const std::string& scenarioFilename)
{
    ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
    config.routing.mode = "global-capacity-aware-hrw";
    ReplayTopologyController controller(config);
    controller.Initialize();

    Check(controller.GetEcmpRouteCandidates(0, 3).size() == 2,
          "capacity path view did not expose both ECMP first hops");
    CheckParallelAdmission(controller);
    CheckDynamicRecovery(controller);
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
        RunCapacityAwareCases(scenarioFilename);
        std::cout << "SatCompute IPv4 capacity-aware routing tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
