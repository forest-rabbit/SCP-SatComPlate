/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/channel.h"
#include "ns3/data-rate.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue-size.h"
#include "ns3/queue.h"
#include "ns3/satellite-id-map.h"
#include "ns3/satellite-ipv4-addressing.h"
#include "ns3/satellite-link-state.h"
#include "ns3/simulator.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
ExpectIdMapError(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const SatelliteIdMapError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

void
ExpectLinkStateError(const std::function<void()>& operation,
                     const std::string& message)
{
    try
    {
        operation();
    }
    catch (const SatelliteLinkStateError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

Ptr<PointToPointNetDevice>
GetPointToPointDevice(const NetDeviceContainer& devices, uint32_t index)
{
    const Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(devices.Get(index));
    Check(device != nullptr, "ISL device type differs");
    return device;
}

uint32_t
GetIpv4Interface(Ptr<PointToPointNetDevice> device)
{
    const int32_t interface =
        device->GetNode()->GetObject<Ipv4>()->GetInterfaceForDevice(device);
    Check(interface >= 0, "ISL IPv4 interface is absent");
    return static_cast<uint32_t>(interface);
}

} // namespace

int
main()
{
    try
    {
        NodeContainer nodes;
        nodes.Create(4);
        const std::vector<uint32_t> idsByNode = {30, 10, 40, 20};
        const SatelliteIdMap idMap(nodes, idsByNode);
        Check(idMap.GetCanonicalSatelliteIds() ==
                  std::vector<uint32_t>({10, 20, 30, 40}),
              "external satellite IDs are not canonical");
        Check(idMap.GetNodeIndexBySatelliteId(10) == 1,
              "external ID was inferred from node creation order");
        Check(idMap.GetSatelliteIdByNodeIndex(1) == 10,
              "reverse external-ID mapping differs");
        Check(idMap.GetNodeBySatelliteId(10)->GetId() != 10,
              "test did not separate external ID from Node::GetId");

        ExpectIdMapError(
            [&nodes] { SatelliteIdMap duplicate(nodes, {1, 1, 2, 3}); },
            "duplicate external satellite IDs were accepted");
        ExpectIdMapError(
            [] {
                NodeContainer empty;
                SatelliteIdMap noNodes(empty, {});
            },
            "empty satellite node set was accepted");

        InternetStackHelper internet;
        internet.Install(nodes);
        const SatelliteIpv4ServiceMap services(idMap);
        Check(services.GetServiceAddress(10) == Ipv4Address("172.16.0.1"),
              "first canonical service address differs");
        Check(services.GetServiceAddress(20) == Ipv4Address("172.16.0.2"),
              "second canonical service address differs");
        Check(services.GetServiceAddress(30) == Ipv4Address("172.16.0.3"),
              "service address depends on ns-3 node order");
        Check(services.GetServiceInterface(10) == 1,
              "legacy-compatible service interface index differs");

        SatelliteLinkState linkState(idMap, 1500, 1500000, false);
        const std::vector<SatelliteLink> initialLinks = {
            {40, 30, 1000000, 100000000},
            {30, 10, 1000000, 100000000},
            {20, 10, 1000000, 100000000},
            {40, 20, 1000000, 100000000},
        };
        const TopologyLinkUpdateSummary initial =
            linkState.ApplyFullSnapshot(initialLinks);
        Check(initial.desiredLinks == 4 && initial.addedLinks == 4,
              "initial ISL summary differs");
        Check(initial.ActiveEdgeSetChanged(),
              "initial active-edge installation was not reported");
        Check(linkState.GetActiveLinks() ==
                  std::vector<std::pair<uint32_t, uint32_t>>(
                      {{10, 20}, {10, 30}, {20, 40}, {30, 40}}),
              "active ISLs are not canonical");
        Check(linkState.GetDirectedLinks().size() == 8,
              "directed ISL interface count differs");
        Check(linkState.GetDirectedLinks().front() == IslDirectedLink{10, 20, 2},
              "first output interface is not legacy compatible");

        const NetDeviceContainer firstLink = linkState.GetLinkDevices(10, 20);
        const Ptr<PointToPointNetDevice> firstDevice =
            GetPointToPointDevice(firstLink, 0);
        const Ptr<PointToPointNetDevice> secondDevice =
            GetPointToPointDevice(firstLink, 1);
        Check(firstDevice->GetMtu() == 1500 && secondDevice->GetMtu() == 1500,
              "ISL MTU differs");
        const uint32_t firstInterface = GetIpv4Interface(firstDevice);
        const uint32_t secondInterface = GetIpv4Interface(secondDevice);
        Check(firstDevice->GetNode()->GetObject<Ipv4>()
                      ->GetAddress(firstInterface, 0)
                      .GetLocal() == Ipv4Address("10.0.0.1"),
              "first deterministic /30 address differs");
        Check(secondDevice->GetNode()->GetObject<Ipv4>()
                      ->GetAddress(secondInterface, 0)
                      .GetLocal() == Ipv4Address("10.0.0.2"),
              "second deterministic /30 address differs");
        Check(firstDevice->GetQueue()->GetMaxSize().GetUnit() ==
                  QueueSizeUnit::BYTES &&
                  firstDevice->GetQueue()->GetMaxSize().GetValue() == 1500000,
              "ISL byte queue capacity differs");

        std::vector<SatelliteLink> delayOnly = initialLinks;
        delayOnly.at(2).delayNs = 2000000;
        delayOnly.at(2).bandwidthBps = 200000000;
        const TopologyLinkUpdateSummary reconfigured =
            linkState.ApplyFullSnapshot(delayOnly);
        Check(reconfigured.retainedLinks == 4 &&
                  reconfigured.reconfiguredLinks == 1,
              "attribute-only update summary differs");
        Check(!reconfigured.ActiveEdgeSetChanged(),
              "attribute-only update changed the active-edge set");

        DataRateValue dataRate;
        firstDevice->GetAttribute("DataRate", dataRate);
        Check(dataRate.Get().GetBitRate() == 200000000,
              "updated ISL data rate differs");
        TimeValue delay;
        firstDevice->GetChannel()->GetAttribute("Delay", delay);
        Check(delay.Get().GetNanoSeconds() == 2000000,
              "updated ISL propagation delay differs");

        const TopologyLinkUpdateSummary identical =
            linkState.ApplyFullSnapshot(delayOnly);
        Check(identical.retainedLinks == 4 &&
                  identical.reconfiguredLinks == 0 &&
                  !identical.ActiveEdgeSetChanged(),
              "identical snapshot was not a topology no-op");

        delayOnly.erase(delayOnly.begin());
        const TopologyLinkUpdateSummary removed =
            linkState.ApplyFullSnapshot(delayOnly);
        Check(removed.disabledLinks == 1 && removed.ActiveEdgeSetChanged(),
              "removed ISL was not reported once");
        Check(!linkState.IsLinkActive(30, 40), "removed ISL remains active");
        const NetDeviceContainer removedDevices = linkState.GetLinkDevices(30, 40);
        Check(!removedDevices.Get(0)
                   ->GetNode()
                   ->GetObject<Ipv4>()
                   ->IsUp(GetIpv4Interface(GetPointToPointDevice(removedDevices, 0))),
              "removed ISL IPv4 interface remains up");

        const TopologyLinkUpdateSummary restored =
            linkState.ApplyFullSnapshot(initialLinks);
        Check(restored.reenabledLinks == 1 && restored.ActiveEdgeSetChanged(),
              "restored ISL was not reported once");
        Check(linkState.GetDirectedLinks().size() == 8,
              "restored ISL created new interface identities");

        const std::vector<SatelliteLink> duplicate = {
            {10, 20, 1, 1},
            {20, 10, 2, 2},
        };
        ExpectLinkStateError(
            [&linkState, &duplicate] { linkState.ApplyFullSnapshot(duplicate); },
            "duplicate undirected ISL was accepted");
        Check(linkState.GetActiveLinks().size() == 4,
              "failed validation partially mutated active links");

        const std::vector<SatelliteLink> unknown = {{10, 999, 1, 1}};
        ExpectLinkStateError(
            [&linkState, &unknown] { linkState.ApplyFullSnapshot(unknown); },
            "unknown external satellite ID was accepted");
        Check(linkState.GetActiveLinks().size() == 4,
              "unknown endpoint partially mutated active links");

        const TopologyLinkUpdateSummary empty = linkState.ApplyFullSnapshot({});
        Check(empty.disabledLinks == 4 && empty.ActiveEdgeSetChanged(),
              "empty active-edge set did not disable every ISL");
        const TopologyLinkUpdateSummary emptyAgain = linkState.ApplyFullSnapshot({});
        Check(!emptyAgain.ActiveEdgeSetChanged(),
              "repeated empty active-edge set was not a no-op");

        Simulator::Destroy();
        std::cout << "SatCompute satellite link-state tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
