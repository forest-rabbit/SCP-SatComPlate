/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/network-transfer-config.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
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

class FakeEndpointView : public SatelliteEndpointView
{
  public:
    FakeEndpointView()
    {
        m_addresses.emplace(10, Ipv4Address("172.16.0.11"));
        m_addresses.emplace(70, Ipv4Address("172.16.0.71"));
    }

    bool
    HasSatelliteId(uint32_t satelliteId) const override
    {
        return m_addresses.contains(satelliteId);
    }

    Ipv4Address
    GetServiceAddress(uint32_t satelliteId) const override
    {
        const auto address = m_addresses.find(satelliteId);
        if (address == m_addresses.end())
        {
            throw std::runtime_error("unknown fake satellite");
        }
        return address->second;
    }

  private:
    std::map<uint32_t, Ipv4Address> m_addresses;
};

bool
SamePlan(const NetworkTransfer& left, const NetworkTransfer& right)
{
    return left.transferId == right.transferId &&
           left.sourceSatelliteId == right.sourceSatelliteId &&
           left.destinationSatelliteId == right.destinationSatelliteId &&
           left.sizeBytes == right.sizeBytes && left.arrivalTimeNs == right.arrivalTimeNs &&
           left.sourceAddress == right.sourceAddress &&
           left.destinationAddress == right.destinationAddress &&
           left.sourcePort == right.sourcePort &&
           left.destinationPort == right.destinationPort &&
           left.payloadBytesPerPacket == right.payloadBytesPerPacket &&
           left.packetCount == right.packetCount &&
           left.finalPacketPayloadBytes == right.finalPacketPayloadBytes;
}

void
ExpectConfigError(const std::filesystem::path& filename, const FakeEndpointView& endpoints)
{
    try
    {
        ReadNetworkTransferTrace(filename, 1000000000, "fixed", 1024, endpoints);
    }
    catch (const NetworkTransferConfigError&)
    {
        return;
    }
    throw std::runtime_error("invalid transfer trace was accepted: " + filename.string());
}

void
CheckCanonicalPlans(const std::filesystem::path& firstFilename,
                    const std::filesystem::path& secondFilename)
{
    const FakeEndpointView endpoints;
    const std::vector<NetworkTransfer> first =
        ReadNetworkTransferTrace(firstFilename, 1000000000, "fixed", 1024, endpoints);
    const std::vector<NetworkTransfer> second =
        ReadNetworkTransferTrace(secondFilename, 1000000000, "fixed", 1024, endpoints);
    Check(first.size() == 2 && second.size() == first.size(),
          "canonical transfer count differs");
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        Check(SamePlan(first[index], second[index]),
              "input array order changed a prepared transfer plan");
    }

    Check(first[0].transferId == 10 && first[0].sourcePort == 10000 &&
              first[0].destinationPort == 9000 && first[0].packetCount == 1 &&
              first[0].finalPacketPayloadBytes == 1024,
          "first canonical transfer derivation differs");
    Check(first[1].transferId == 20 && first[1].sourcePort == 10001 &&
              first[1].packetCount == 3 && first[1].finalPacketPayloadBytes == 2,
          "second canonical transfer derivation differs");
    Check(first[0].sourceAddress == Ipv4Address("172.16.0.11") &&
              first[0].destinationAddress == Ipv4Address("172.16.0.71"),
          "stable endpoint address derivation differs");

    const EcmpFlowKey firstKey = BuildNetworkTransferFlowKey(first[0]);
    const EcmpFlowKey secondKey = BuildNetworkTransferFlowKey(first[1]);
    Check(firstKey.sourcePort == 10000 && secondKey.sourcePort == 10001 &&
              firstKey.destinationPort == NETWORK_TRANSFER_DESTINATION_PORT,
          "prepared transfer five-tuples differ");
}

void
CheckChunkBoundaries()
{
    Check(ResolveNetworkTransferPayloadBytes("fixed", 1400, 1) == 1400,
          "fixed chunk size differs");
    Check(ResolveNetworkTransferPayloadBytes("size-aware", 1400, 1ULL << 20) == 1024,
          "small size-aware boundary differs");
    Check(ResolveNetworkTransferPayloadBytes("size-aware", 1400, (1ULL << 20) + 1) == 8192,
          "medium size-aware lower boundary differs");
    Check(ResolveNetworkTransferPayloadBytes("size-aware", 1400, 64ULL << 20) == 8192,
          "medium size-aware upper boundary differs");
    Check(ResolveNetworkTransferPayloadBytes("size-aware", 1400, (64ULL << 20) + 1) ==
              GetSizeAwareMaximumPayloadBytes(),
          "large size-aware boundary differs");
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string canonicalA;
    std::string canonicalB;
    std::string invalidUnknownField;
    std::string invalidDuplicateId;
    std::string invalidUnknownSatellite;
    std::string invalidStopTime;
    CommandLine command(__FILE__);
    command.AddValue("canonicalA", "First canonical trace", canonicalA);
    command.AddValue("canonicalB", "Second canonical trace", canonicalB);
    command.AddValue("invalidUnknownField", "Unknown-field trace", invalidUnknownField);
    command.AddValue("invalidDuplicateId", "Duplicate-ID trace", invalidDuplicateId);
    command.AddValue("invalidUnknownSatellite", "Unknown-satellite trace", invalidUnknownSatellite);
    command.AddValue("invalidStopTime", "Stop-time trace", invalidStopTime);
    command.Parse(argc, argv);

    try
    {
        Check(!canonicalA.empty() && !canonicalB.empty(), "canonical traces are required");
        CheckCanonicalPlans(canonicalA, canonicalB);
        CheckChunkBoundaries();
        const FakeEndpointView endpoints;
        ExpectConfigError(invalidUnknownField, endpoints);
        ExpectConfigError(invalidDuplicateId, endpoints);
        ExpectConfigError(invalidUnknownSatellite, endpoints);
        ExpectConfigError(invalidStopTime, endpoints);
        std::cout << "SatCompute transfer trace tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
