/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/flow-route-registry.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer-config.h"
#include "ns3/network-transfer-engine.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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
ResetSimulationGlobals()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

void
CheckCompletedSummary(const NetworkTransferEngine& engine,
                      uint64_t expectedBytes,
                      uint64_t expectedPackets)
{
    if (!engine.AreAllTransfersCompleted())
    {
        std::ostringstream detail;
        for (const TransferSummaryRecord& summary : engine.CollectSummaries())
        {
            detail << " id=" << summary.transferId << " state=" << summary.transferState
                   << " sent=" << summary.sentApplicationBytes
                   << " received=" << summary.receivedApplicationBytes;
        }
        throw std::runtime_error("transfer engine did not complete every flow:" + detail.str());
    }
    const ApplicationMetrics metrics = engine.CollectApplicationMetrics();
    Check(metrics.sinkApplications == 1 && metrics.sentBytes == expectedBytes &&
              metrics.receivedBytes == expectedBytes,
          "transfer aggregate metrics differ");

    const std::vector<TransferSummaryRecord> summaries = engine.CollectSummaries();
    Check(summaries.size() == 2, "transfer summary count differs");
    uint64_t summaryBytes = 0;
    uint64_t summaryPackets = 0;
    for (const TransferSummaryRecord& summary : summaries)
    {
        Check(summary.transferState == "COMPLETED",
              "completed transfer has an incorrect state label");
        Check(summary.sentApplicationBytes == summary.declaredSizeBytes &&
                  summary.receivedApplicationBytes == summary.declaredSizeBytes,
              "completed transfer payload totals differ");
        Check(summary.sentPacketCount == summary.derivedPacketCount &&
                  summary.receivedPacketCount == summary.derivedPacketCount,
              "completed transfer packet totals differ");
        Check(summary.arrivalTimeNs == 100000000 && summary.lastSendTimeNs >= 100000000 &&
                  summary.completionTimeNs >= summary.lastSendTimeNs &&
                  summary.completionDelayNs ==
                      summary.completionTimeNs - summary.arrivalTimeNs,
              "completed transfer timing fields differ");
        summaryBytes += summary.declaredSizeBytes;
        summaryPackets += summary.derivedPacketCount;
    }
    Check(summaryBytes == expectedBytes && summaryPackets == expectedPackets,
          "canonical transfer summary totals differ");

    const std::vector<TransferFlowMetadata> flowMetadata = engine.CollectFlowMetadata();
    Check(flowMetadata.size() == 2 &&
              flowMetadata[0].sourcePort == NETWORK_TRANSFER_FIRST_SOURCE_PORT &&
              flowMetadata[1].sourcePort == NETWORK_TRANSFER_FIRST_SOURCE_PORT + 1 &&
              flowMetadata[0].receivedApplicationPayloadBytes ==
                  flowMetadata[0].plannedApplicationPayloadBytes,
          "stable transfer flow metadata differs");
    Check(engine.CollectUdpSocketDropEvents().empty(),
          "lossless transfer fixture recorded a UDP socket drop");
}

void
RunBasicMode(const std::string& scenarioFilename,
             const std::string& transferFilename,
             const std::string& routingMode)
{
    {
        ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        config.routing.mode = routingMode;
        ReplayTopologyController controller(config);
        controller.Initialize();

        std::vector<NetworkTransfer> plans = ReadNetworkTransferTrace(
            transferFilename,
            config.simulation.durationNs,
            config.workloads.transferChunkMode,
            config.workloads.transferPayloadBytes,
            controller);
        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        engine->Configure(controller,
                          config.workloads.transferChunkMode,
                          config.workloads.transferPayloadBytes,
                          config.network.islMtuBytes,
                          config.network.receiverRcvBufBytes,
                          true,
                          config.simulation.durationNs);
        engine->RegisterPlans(std::move(plans));
        engine->ScheduleDeclaredTransfers();

        Simulator::Stop(NanoSeconds(1000000000));
        Simulator::Run();

        CheckCompletedSummary(*engine, 3074, 4);
        Ptr<FlowRouteRegistry> registry = controller.GetFlowRouteRegistry();
        if (registry != nullptr)
        {
            Check(registry->GetRegisteredFlowCount() == 2 &&
                      registry->GetActiveFlowCount() == 0 &&
                      registry->GetAssignmentCount() == 0 &&
                      registry->GetTotalReservedBytes() == 0,
                  "reservation-aware basic transfer leaked routing state");
        }
        if (controller.IsCapacityAwareRouting())
        {
            const CapacityAwareRuntimeSummary capacity =
                engine->CollectCapacityAwareSummary();
            Check(capacity.activePathCountAtEnd == 0 &&
                      capacity.reservedDirectedLinkCountAtEnd == 0 &&
                      capacity.totalReservedRateBpsAtEnd == 0 &&
                      capacity.pendingTransferCountAtEnd == 0,
                  "capacity-aware basic transfer leaked path state");
        }
    }
    ResetSimulationGlobals();
}

void
RunCapacityPending(const std::string& scenarioFilename,
                   const std::string& transferFilename)
{
    {
        ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        ReplayTopologyController controller(config);
        controller.Initialize();

        std::vector<NetworkTransfer> plans = ReadNetworkTransferTrace(
            transferFilename,
            config.simulation.durationNs,
            config.workloads.transferChunkMode,
            config.workloads.transferPayloadBytes,
            controller);
        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        engine->Configure(controller,
                          config.workloads.transferChunkMode,
                          config.workloads.transferPayloadBytes,
                          config.network.islMtuBytes,
                          config.network.receiverRcvBufBytes,
                          true,
                          config.simulation.durationNs);
        engine->RegisterPlans(std::move(plans));
        engine->ScheduleDeclaredTransfers();

        bool waitingObserved = false;
        bool admissionObserved = false;
        Simulator::Schedule(NanoSeconds(500000000), [&engine, &waitingObserved] {
            const CapacityAwareRuntimeSummary capacity =
                engine->CollectCapacityAwareSummary();
            const TransferSummaryRecord summary = engine->CollectSummaries().front();
            Check(capacity.activePathCountAtEnd == 0 &&
                      capacity.pendingTransferCountAtEnd == 1 &&
                      summary.transferState == "STARTED" &&
                      summary.sentApplicationBytes == 0,
                  "capacity-aware transfer did not wait while no path existed");
            waitingObserved = true;
        });
        Simulator::Schedule(NanoSeconds(1100000000),
                            [&engine, &admissionObserved] {
                                const CapacityAwareRuntimeSummary capacity =
                                    engine->CollectCapacityAwareSummary();
                                const TransferSummaryRecord summary =
                                    engine->CollectSummaries().front();
                                Check(capacity.activePathCountAtEnd == 1 &&
                                          capacity.pendingTransferCountAtEnd == 0 &&
                                          summary.sentApplicationBytes > 0,
                                      "route update did not admit the waiting transfer");
                                admissionObserved = true;
                            });
        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(waitingObserved && admissionObserved,
              "capacity-aware pending checkpoints did not both run");
        Check(engine->AreAllTransfersCompleted(),
              "capacity-aware pending transfer did not complete");
        const TransferSummaryRecord summary = engine->CollectSummaries().front();
        Check(summary.declaredSizeBytes == 100000 &&
                  summary.sentApplicationBytes == 100000 &&
                  summary.receivedApplicationBytes == 100000 &&
                  summary.derivedPacketCount == 72 && summary.sentPacketCount == 72 &&
                  summary.receivedPacketCount == 72 &&
                  summary.transferState == "COMPLETED" &&
                  summary.completionTimeNs > 1000000000,
              "capacity-aware pending summary differs");
        const ApplicationMetrics metrics = engine->CollectApplicationMetrics();
        Check(metrics.sinkApplications == 1 && metrics.sentBytes == 100000 &&
                  metrics.receivedBytes == 100000,
              "capacity-aware pending aggregate metrics differ");
        const CapacityAwareRuntimeSummary capacity = engine->CollectCapacityAwareSummary();
        Check(capacity.activePathCountAtEnd == 0 &&
                  capacity.reservedDirectedLinkCountAtEnd == 0 &&
                  capacity.totalReservedRateBpsAtEnd == 0 &&
                  capacity.pendingTransferCountAtEnd == 0,
              "capacity-aware pending transfer leaked admission state");

        Check(controller.GetRouteComputationCount() == 2,
              "capacity-aware pending topology rebuilt routes unexpectedly");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenarioFilename;
    std::string capacityScenarioFilename;
    std::string basicTransferFilename;
    std::string capacityTransferFilename;
    CommandLine command(__FILE__);
    command.AddValue("scenario", "Dynamic diamond replay scenario", scenarioFilename);
    command.AddValue("capacityScenario",
                     "Initially disconnected capacity scenario",
                     capacityScenarioFilename);
    command.AddValue("basicTransfers", "Basic transfer trace", basicTransferFilename);
    command.AddValue("capacityTransfers", "Capacity pending trace", capacityTransferFilename);
    command.Parse(argc, argv);

    try
    {
        Check(!scenarioFilename.empty() && !capacityScenarioFilename.empty() &&
                  !basicTransferFilename.empty() &&
                  !capacityTransferFilename.empty(),
              "scenario and transfer fixture paths are required");
        for (const char* mode : {"global-first",
                                 "global-hash-per-flow",
                                 "global-hrw-per-flow",
                                 "global-size-aware-hrw",
                                 "global-capacity-aware-hrw"})
        {
            try
            {
                RunBasicMode(scenarioFilename, basicTransferFilename, mode);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(std::string(mode) + ": " + error.what());
            }
        }
        RunCapacityPending(capacityScenarioFilename, capacityTransferFilename);
        std::cout << "SatCompute UDP transfer engine tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
