/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/network-transfer-config.h"
#include "ns3/snapshot-reader.h"
#include "ns3/snapshot-schedule.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
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

class Synthetic66EndpointView : public SatelliteEndpointView
{
  public:
    bool
    HasSatelliteId(uint32_t satelliteId) const override
    {
        return satelliteId < 66;
    }

    Ipv4Address
    GetServiceAddressBySatelliteId(uint32_t satelliteId) const override
    {
        if (!HasSatelliteId(satelliteId))
        {
            throw std::runtime_error("unknown synthetic-66 satellite");
        }
        return Ipv4Address(0xac100001u + satelliteId);
    }
};

void
CheckReplayInputs(const std::filesystem::path& dynamicDirectory,
                  const std::filesystem::path& staticDirectory)
{
    constexpr int64_t nanosecondsPerSecond = 1000000000LL;
    const SnapshotSchedule dynamic = ScanSatelliteSnapshots(dynamicDirectory,
                                                            120 * nanosecondsPerSecond,
                                                            10 * nanosecondsPerSecond);
    Check(!dynamic.manifestAuthoritative && dynamic.discoveredSnapshotCount == 12 &&
              dynamic.selectedSnapshotCount == 12 && dynamic.updates.size() == 11,
          "xw-66 dynamic replay schedule differs");
    const SatelliteSnapshot dynamicInitial =
        ReadSatelliteSnapshot(dynamic.initialNodesFilename, dynamic.initialLinksFilename, 0);
    Check(dynamicInitial.schema == SatelliteSnapshotSchema::LEGACY &&
              dynamicInitial.satelliteIds.size() == 66 &&
              dynamicInitial.links.size() == 132 && dynamicInitial.positions.empty(),
          "xw-66 dynamic replay content differs");

    const SnapshotSchedule fixed = ScanSatelliteSnapshots(staticDirectory,
                                                          nanosecondsPerSecond,
                                                          20 * nanosecondsPerSecond);
    Check(!fixed.manifestAuthoritative && fixed.discoveredSnapshotCount == 1 &&
              fixed.selectedSnapshotCount == 1 && fixed.updates.empty(),
          "xw-66 static replay schedule differs");
    const SatelliteSnapshot fixedInitial =
        ReadSatelliteSnapshot(fixed.initialNodesFilename, fixed.initialLinksFilename, 0);
    Check(fixedInitial.satelliteIds.size() == 66 && fixedInitial.links.size() == 132,
          "xw-66 static replay content differs");
}

void
CheckComputeInputs(const std::filesystem::path& selectedProfile,
                   const std::filesystem::path& allProfile)
{
    const Synthetic66EndpointView endpoints;
    const ComputeProfile selected = ReadComputeProfile(selectedProfile, endpoints);
    const ComputeProfile all = ReadComputeProfile(allProfile, endpoints);
    Check(selected.nodes.size() == 22 && selected.nodes.front().nodeId == 0 &&
              selected.nodes.back().nodeId == 63,
          "selected synthetic-66 compute profile differs");
    Check(all.nodes.size() == 66 && all.nodes.front().nodeId == 0 &&
              all.nodes.back().nodeId == 65,
          "all-node synthetic-66 compute profile differs");
}

void
CheckTransferInputs(const std::filesystem::path& variedWorkload,
                    const std::filesystem::path& largeWorkload)
{
    const Synthetic66EndpointView endpoints;
    const std::vector<NetworkTransfer> varied = ReadNetworkTransferTrace(
        variedWorkload,
        std::numeric_limits<int64_t>::max(),
        "fixed",
        1024,
        endpoints);
    const std::vector<NetworkTransfer> large = ReadNetworkTransferTrace(
        largeWorkload,
        std::numeric_limits<int64_t>::max(),
        "fixed",
        1024,
        endpoints);
    Check(varied.size() == 5000 && varied.front().transferId == 1 &&
              varied.back().transferId == 5000,
          "5000-transfer workload differs");
    Check(large.size() == 10 && large.front().sizeBytes == 134217728ULL &&
              large.back().sizeBytes == 1073741824ULL,
          "large local workload differs");
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string dynamicTopology;
    std::string staticTopology;
    std::string selectedComputeProfile;
    std::string allComputeProfile;
    std::string variedWorkload;
    std::string largeWorkload;
    CommandLine command(__FILE__);
    command.AddValue("dynamicTopology", "xw-66 10-second replay directory", dynamicTopology);
    command.AddValue("staticTopology", "xw-66 static replay directory", staticTopology);
    command.AddValue("selectedComputeProfile",
                     "xw-66 selected-node compute profile",
                     selectedComputeProfile);
    command.AddValue("allComputeProfile", "xw-66 all-node compute profile", allComputeProfile);
    command.AddValue("variedWorkload", "5000-transfer workload", variedWorkload);
    command.AddValue("largeWorkload", "large local workload", largeWorkload);
    command.Parse(argc, argv);

    try
    {
        Check(!dynamicTopology.empty() && !staticTopology.empty() &&
                  !selectedComputeProfile.empty() && !allComputeProfile.empty() &&
                  !variedWorkload.empty() && !largeWorkload.empty(),
              "all input-contract paths are required");
        CheckReplayInputs(dynamicTopology, staticTopology);
        CheckComputeInputs(selectedComputeProfile, allComputeProfile);
        CheckTransferInputs(variedWorkload, largeWorkload);
        std::cout << "SatCompute restored input contract tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
