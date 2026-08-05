/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"

#include "../support/config-factory.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeDiamondReplayTestConfig;
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
ResetSimulationGlobals()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

void
RunReplayFacadeCase(const std::filesystem::path& topologyDirectory)
{
    {
        const ResolvedSatComputeConfig config =
            MakeDiamondReplayTestConfig(topologyDirectory);
        SatelliteTopology topology(config);
        topology.Initialize();

        Check(topology.GetConfig().network.topologySource == "replay",
              "facade selected the wrong replay controller");
        Check(topology.GetNodeCount() == 4, "facade replay node count differs");
        Check(topology.GetNode(0) == topology.GetNodeBySatelliteId(0),
              "facade node-index mapping differs");
        Check(topology.GetNodeIndexBySatelliteId(3) == 3 &&
                  topology.GetSatelliteIdByNodeIndex(3) == 3,
              "facade stable satellite-ID mapping differs");
        Check(topology.GetServiceAddress(0) == Ipv4Address("172.16.0.1") &&
                  topology.GetServiceAddressBySatelliteId(3) ==
                      Ipv4Address("172.16.0.4"),
              "facade service-address views differ");
        Check(topology.GetEcmpCandidateSatelliteIds(0, 3) ==
                  std::vector<uint32_t>({1, 2}),
              "facade ECMP next-hop audit differs");
        Check(topology.GetIslDirectedLinks().size() == 8,
              "facade directed-ISL view differs");
        Check(topology.GetEcmpHashSeed() == config.routing.hashSeed,
              "facade ECMP seed differs");

        try
        {
            topology.GetOnlineConstellation();
        }
        catch (const SatelliteTopologyError&)
        {
            Simulator::Stop(NanoSeconds(config.simulation.durationNs));
            Simulator::Run();
            Check(topology.GetAppliedTopologySliceCount() == 3,
                  "facade replay slice count differs");
            Check(topology.GetRouteComputationCount() == 3,
                  "facade replay route count differs");
            return;
        }
        throw std::runtime_error("replay facade exposed online orbit state");
    }
}

void
RunOnlineFacadeCase()
{
    {
        const ResolvedSatComputeConfig config =
            MakeOnlineTestConfig(2,
                                 3,
                                 "fixed",
                                 2500000000LL,
                                 1000000000LL,
                                 30000000.0L);
        SatelliteTopology topology(config);
        topology.Initialize();

        Check(topology.GetConfig().network.topologySource == "online",
              "facade selected the wrong online controller");
        Check(topology.GetNodeCount() == 6, "facade online node count differs");
        Check(topology.GetOnlineConstellation().GetOrbitIdentities().size() == 6,
              "facade online orbit view differs");

        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();
        Check(topology.GetAppliedTopologySliceCount() == 3,
              "facade online update count differs");
        Check(topology.GetRouteComputationCount() == 1,
              "unchanged facade online topology rebuilt routes");
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string topologyDirectory;
    CommandLine command(__FILE__);
    command.AddValue("topologyDir", "Four-node dynamic topology slices", topologyDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!topologyDirectory.empty(), "topologyDir is required");
        RunReplayFacadeCase(topologyDirectory);
        ResetSimulationGlobals();
        RunOnlineFacadeCase();
        ResetSimulationGlobals();
        std::cout << "SatCompute SatelliteTopology facade tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
