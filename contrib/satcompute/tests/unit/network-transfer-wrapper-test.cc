/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"

#include "../support/config-factory.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeDiamondReplayTestConfig;

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
CheckLegacyWrapper(const std::string& topologyDirectory,
                   const std::string& transferFilename)
{
    ResolvedSatComputeConfig config = MakeDiamondReplayTestConfig(topologyDirectory);
    SatelliteTopology topology(config);
    topology.Initialize();

    std::ostringstream log;
    std::streambuf* previousBuffer = std::cout.rdbuf(log.rdbuf());
    const NetworkTransferState state = InstallNetworkTransfers(
        transferFilename,
        config.workloads.transferChunkMode,
        config.workloads.transferPayloadBytes,
        config.network.islMtuBytes,
        config.network.receiverRcvBufBytes,
        true,
        "summary",
        5.0,
        topology);
    std::cout.rdbuf(previousBuffer);

    const std::string output = log.str();
    Check(output.find("[TRANSFER:SUMMARY]") != std::string::npos &&
              output.find("transfers                   : 2") != std::string::npos &&
              output.find("total application bytes     : 3074") != std::string::npos &&
              output.find("pacing mode                 : first-hop-serialization") !=
                  std::string::npos,
          "legacy wrapper summary log differs");

    const std::vector<NetworkTransfer>& plans = state.engine->GetPlans();
    Check(plans.size() == 2 && plans[0].transferId == 10 && plans[1].transferId == 20 &&
              plans[0].sourcePort == NETWORK_TRANSFER_FIRST_SOURCE_PORT &&
              plans[1].sourcePort == NETWORK_TRANSFER_FIRST_SOURCE_PORT + 1,
          "legacy wrapper did not preserve canonical transfer preparation");

    Simulator::Stop(NanoSeconds(1000000000));
    Simulator::Run();

    const ApplicationMetrics metrics = CollectNetworkTransferMetrics(state);
    Check(metrics.sinkApplications == 1 && metrics.sentBytes == 3074 &&
              metrics.receivedBytes == 3074,
          "legacy wrapper aggregate metrics differ");
    const std::vector<TransferFlowMetadata> flows =
        CollectNetworkTransferFlowMetadata(state);
    Check(flows.size() == 2 && flows[0].transferId == 10 && flows[1].transferId == 20 &&
              flows[0].receivedApplicationPayloadBytes ==
                  flows[0].plannedApplicationPayloadBytes,
          "legacy wrapper flow metadata differs");
    const std::vector<TransferSummaryRecord> summaries =
        CollectNetworkTransferSummaries(state);
    Check(summaries.size() == 2 && summaries[0].transferId == 10 &&
              summaries[1].transferId == 20 &&
              summaries[0].transferState == "COMPLETED" &&
              summaries[1].transferState == "COMPLETED",
          "legacy wrapper transfer summaries differ");
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string topologyDirectory;
    std::string transferFilename;
    CommandLine command(__FILE__);
    command.AddValue("topologyDir", "Dynamic diamond topology slices", topologyDirectory);
    command.AddValue("transfers", "Canonical transfer trace", transferFilename);
    command.Parse(argc, argv);

    try
    {
        Check(!topologyDirectory.empty() && !transferFilename.empty(),
              "topologyDir and transfers are required");
        {
            CheckLegacyWrapper(topologyDirectory, transferFilename);
        }
        ResetSimulationGlobals();
        std::cout << "SatCompute network transfer wrapper tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
