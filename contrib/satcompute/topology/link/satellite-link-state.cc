/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "satellite-link-state.h"

#include "ns3/callback.h"
#include "ns3/data-rate.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4.h"
#include "ns3/point-to-point-module.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <limits>
#include <string>

namespace ns3
{

bool
TopologyLinkUpdateSummary::ActiveEdgeSetChanged() const
{
    return addedLinks != 0 || reenabledLinks != 0 || disabledLinks != 0;
}

SatelliteLinkState::SatelliteLinkState(const SatelliteIdMap& idMap,
                                       uint16_t islMtuBytes,
                                       uint32_t islQueueBytes,
                                       bool collectQueueDrops)
    : m_idMap(idMap),
      m_islMtuBytes(islMtuBytes),
      m_islQueueBytes(islQueueBytes),
      m_collectQueueDrops(collectQueueDrops)
{
    if (m_islMtuBytes < 68)
    {
        throw SatelliteLinkStateError("ISL MTU must be at least 68 bytes");
    }
    if (m_islQueueBytes == 0)
    {
        throw SatelliteLinkStateError("ISL queue must be at least 1 byte");
    }
    for (const uint32_t satelliteId : m_idMap.GetCanonicalSatelliteIds())
    {
        if (m_idMap.GetNodeBySatelliteId(satelliteId)->GetObject<Ipv4>() == nullptr)
        {
            throw SatelliteLinkStateError(
                "satellite " + std::to_string(satelliteId) +
                " has no IPv4 stack before ISL construction");
        }
    }
}

SatelliteLinkState::LinkKey
SatelliteLinkState::MakeKey(uint32_t firstSatelliteId, uint32_t secondSatelliteId) const
{
    return std::minmax(firstSatelliteId, secondSatelliteId);
}

std::vector<SatelliteLink>
SatelliteLinkState::ValidateAndCanonicalize(const std::vector<SatelliteLink>& links) const
{
    if (links.size() > std::numeric_limits<uint32_t>::max())
    {
        throw SatelliteLinkStateError("active ISL count exceeds uint32 range");
    }

    std::vector<SatelliteLink> canonical = links;
    for (SatelliteLink& link : canonical)
    {
        if (link.sourceId == link.destinationId)
        {
            throw SatelliteLinkStateError("ISL cannot connect a satellite to itself");
        }
        if (!m_idMap.HasSatelliteId(link.sourceId) ||
            !m_idMap.HasSatelliteId(link.destinationId))
        {
            throw SatelliteLinkStateError(
                "ISL references an unknown external satellite ID");
        }
        if (link.bandwidthBps == 0)
        {
            throw SatelliteLinkStateError("ISL bandwidth must be positive");
        }
        if (link.delayNs < 0)
        {
            throw SatelliteLinkStateError(
                "ISL delay must be non-negative integer nanoseconds");
        }
        const LinkKey key = MakeKey(link.sourceId, link.destinationId);
        link.sourceId = key.first;
        link.destinationId = key.second;
    }
    std::sort(canonical.begin(),
              canonical.end(),
              [](const SatelliteLink& left, const SatelliteLink& right) {
                  return std::pair{left.sourceId, left.destinationId} <
                         std::pair{right.sourceId, right.destinationId};
              });
    for (std::size_t index = 1; index < canonical.size(); ++index)
    {
        if (MakeKey(canonical[index - 1].sourceId, canonical[index - 1].destinationId) ==
            MakeKey(canonical[index].sourceId, canonical[index].destinationId))
        {
            throw SatelliteLinkStateError(
                "active snapshot contains a duplicate undirected ISL");
        }
    }
    return canonical;
}

void
SatelliteLinkState::AssignIpv4Addresses(const NetDeviceContainer& devices)
{
    constexpr uint32_t networksInTenSlashEight = 1u << 22;
    if (m_nextIpv4Network >= networksInTenSlashEight)
    {
        throw SatelliteLinkStateError(
            "10.0.0.0/8 contains no remaining /30 ISL network");
    }

    const uint32_t rawAddress = 0x0a000000u + m_nextIpv4Network * 4u;
    ++m_nextIpv4Network;

    Ipv4AddressHelper ipv4;
    ipv4.SetBase(Ipv4Address(rawAddress), Ipv4Mask("255.255.255.252"));
    ipv4.Assign(devices);
}

void
SatelliteLinkState::ConfigureLink(const NetDeviceContainer& devices,
                                  const SatelliteLink& link) const
{
    const DataRateValue dataRate{DataRate(link.bandwidthBps)};
    for (uint32_t index = 0; index < devices.GetN(); ++index)
    {
        const Ptr<PointToPointNetDevice> device =
            DynamicCast<PointToPointNetDevice>(devices.Get(index));
        if (device == nullptr)
        {
            throw SatelliteLinkStateError(
                "installed ISL device is not PointToPointNetDevice");
        }
        if (!device->SetMtu(m_islMtuBytes))
        {
            throw SatelliteLinkStateError("cannot set ISL MTU");
        }
        device->SetAttribute("DataRate", dataRate);
    }

    const Ptr<PointToPointNetDevice> first =
        DynamicCast<PointToPointNetDevice>(devices.Get(0));
    const Ptr<PointToPointChannel> channel =
        DynamicCast<PointToPointChannel>(first->GetChannel());
    if (channel == nullptr)
    {
        throw SatelliteLinkStateError(
            "installed ISL channel is not PointToPointChannel");
    }
    channel->SetAttribute(
        "Delay",
        TimeValue(NanoSeconds(link.delayNs)));
}

void
SatelliteLinkState::ConnectQueueDropTrace(Ptr<NetDevice> netDevice,
                                          uint32_t sourceSatelliteId,
                                          uint32_t destinationSatelliteId)
{
    const Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(netDevice);
    if (device == nullptr)
    {
        throw SatelliteLinkStateError(
            "installed ISL device is not PointToPointNetDevice");
    }
    const Ptr<Ipv4> ipv4 = device->GetNode()->GetObject<Ipv4>();
    const int32_t interface = ipv4->GetInterfaceForDevice(device);
    if (interface < 0)
    {
        throw SatelliteLinkStateError("installed ISL has no IPv4 interface");
    }

    const IslDirectedLink directedLink = {
        sourceSatelliteId,
        destinationSatelliteId,
        static_cast<uint32_t>(interface),
    };
    m_directedLinks.push_back(directedLink);

    if (!m_collectQueueDrops)
    {
        return;
    }
    const Ptr<Queue<Packet>> queue = device->GetQueue();
    if (queue == nullptr)
    {
        throw SatelliteLinkStateError("installed ISL has no transmit queue");
    }
    const bool connected = queue->TraceConnectWithoutContext(
        "Drop",
        MakeBoundCallback(&SatelliteLinkState::QueueDropCallback, this, directedLink));
    if (!connected)
    {
        throw SatelliteLinkStateError("cannot connect ISL queue Drop trace");
    }
}

void
SatelliteLinkState::QueueDropCallback(SatelliteLinkState* state,
                                      IslDirectedLink directedLink,
                                      Ptr<const Packet> packet)
{
    state->RecordQueueDrop(directedLink.sourceSatelliteId,
                           directedLink.destinationSatelliteId,
                           directedLink.outputInterface,
                           packet);
}

void
SatelliteLinkState::RecordQueueDrop(uint32_t sourceSatelliteId,
                                    uint32_t destinationSatelliteId,
                                    uint32_t outputInterface,
                                    Ptr<const Packet> packet)
{
    if (packet == nullptr)
    {
        throw SatelliteLinkStateError("ISL queue Drop trace received a null packet");
    }
    const DirectedQueueKey key = std::make_pair(sourceSatelliteId, outputInterface);
    auto& totals = m_queueDropTotals[key];
    ++totals.first;
    totals.second += packet->GetSize();
    m_queueDropEvents.push_back(
        {Simulator::Now().GetNanoSeconds(),
         sourceSatelliteId,
         destinationSatelliteId,
         outputInterface,
         packet->GetSize(),
         totals.first,
         totals.second});
}

NetDeviceContainer
SatelliteLinkState::InstallLink(const SatelliteLink& link)
{
    PointToPointHelper helper;
    helper.SetDeviceAttribute("DataRate",
                              DataRateValue(DataRate(link.bandwidthBps)));
    helper.SetDeviceAttribute("Mtu", UintegerValue(m_islMtuBytes));
    helper.SetChannelAttribute(
        "Delay",
        TimeValue(NanoSeconds(link.delayNs)));
    helper.SetQueue("ns3::DropTailQueue",
                    "MaxSize",
                    StringValue(std::to_string(m_islQueueBytes) + "B"));

    const NetDeviceContainer devices = helper.Install(
        NodeContainer(m_idMap.GetNodeBySatelliteId(link.sourceId),
                      m_idMap.GetNodeBySatelliteId(link.destinationId)));
    AssignIpv4Addresses(devices);
    ConnectQueueDropTrace(devices.Get(0), link.sourceId, link.destinationId);
    ConnectQueueDropTrace(devices.Get(1), link.destinationId, link.sourceId);
    return devices;
}

void
SatelliteLinkState::SetLinkState(const NetDeviceContainer& devices, bool isUp) const
{
    for (uint32_t index = 0; index < devices.GetN(); ++index)
    {
        const Ptr<NetDevice> device = devices.Get(index);
        const Ptr<Ipv4> ipv4 = device->GetNode()->GetObject<Ipv4>();
        const int32_t interface = ipv4->GetInterfaceForDevice(device);
        if (interface < 0)
        {
            throw SatelliteLinkStateError("installed ISL has no IPv4 interface");
        }

        if (isUp)
        {
            ipv4->SetUp(static_cast<uint32_t>(interface));
        }
        else
        {
            ipv4->SetDown(static_cast<uint32_t>(interface));
        }
    }
}

TopologyLinkUpdateSummary
SatelliteLinkState::ApplyFullSnapshot(const std::vector<SatelliteLink>& links)
{
    const std::vector<SatelliteLink> canonical = ValidateAndCanonicalize(links);
    TopologyLinkUpdateSummary summary;
    summary.desiredLinks = static_cast<uint32_t>(canonical.size());
    std::set<LinkKey> desiredLinks;

    for (const SatelliteLink& link : canonical)
    {
        const LinkKey key = MakeKey(link.sourceId, link.destinationId);
        desiredLinks.insert(key);

        const auto installed = m_installedLinks.find(key);
        if (installed == m_installedLinks.end())
        {
            const NetDeviceContainer devices = InstallLink(link);
            m_installedLinks.emplace(key, devices);
            m_linkDefinitions.emplace(key, link);
            m_activeLinks.insert(key);
            ++summary.addedLinks;
            continue;
        }

        const bool attributesChanged = m_linkDefinitions.at(key) != link;
        if (attributesChanged)
        {
            ConfigureLink(installed->second, link);
            m_linkDefinitions.at(key) = link;
            ++summary.reconfiguredLinks;
        }
        if (!m_activeLinks.contains(key))
        {
            SetLinkState(installed->second, true);
            m_activeLinks.insert(key);
            ++summary.reenabledLinks;
        }
        else
        {
            ++summary.retainedLinks;
        }
    }

    std::vector<LinkKey> linksToDisable;
    for (const LinkKey& active : m_activeLinks)
    {
        if (!desiredLinks.contains(active))
        {
            linksToDisable.push_back(active);
        }
    }
    for (const LinkKey& key : linksToDisable)
    {
        SetLinkState(m_installedLinks.at(key), false);
        m_activeLinks.erase(key);
        ++summary.disabledLinks;
    }
    return summary;
}

void
SatelliteLinkState::PrepareCandidateLinks(const std::vector<SatelliteLink>& links)
{
    if (!m_installedLinks.empty() || !m_activeLinks.empty())
    {
        throw SatelliteLinkStateError(
            "candidate links must be prepared before the first active snapshot");
    }
    const std::vector<SatelliteLink> canonical = ValidateAndCanonicalize(links);
    for (const SatelliteLink& link : canonical)
    {
        const LinkKey key = MakeKey(link.sourceId, link.destinationId);
        const NetDeviceContainer devices = InstallLink(link);
        SetLinkState(devices, false);
        m_installedLinks.emplace(key, devices);
        m_linkDefinitions.emplace(key, link);
    }
}

bool
SatelliteLinkState::IsLinkActive(uint32_t firstSatelliteId,
                                 uint32_t secondSatelliteId) const
{
    return m_activeLinks.contains(MakeKey(firstSatelliteId, secondSatelliteId));
}

NetDeviceContainer
SatelliteLinkState::GetLinkDevices(uint32_t firstSatelliteId,
                                   uint32_t secondSatelliteId) const
{
    const auto link = m_installedLinks.find(MakeKey(firstSatelliteId, secondSatelliteId));
    if (link == m_installedLinks.end())
    {
        throw SatelliteLinkStateError("requested ISL has never been installed");
    }
    return link->second;
}

std::vector<std::pair<uint32_t, uint32_t>>
SatelliteLinkState::GetActiveLinks() const
{
    return {m_activeLinks.begin(), m_activeLinks.end()};
}

const std::vector<IslDirectedLink>&
SatelliteLinkState::GetDirectedLinks() const
{
    return m_directedLinks;
}

const std::vector<IslQueueDropEvent>&
SatelliteLinkState::GetQueueDropEvents() const
{
    return m_queueDropEvents;
}

} // namespace ns3
