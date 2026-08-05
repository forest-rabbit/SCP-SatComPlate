/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/network-transfer-engine.h"
#include "ns3/online-topology-controller.h"
#include "ns3/simulator.h"

#include "../support/config-factory.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using satcompute::test::MakeOnlineTestConfig;
using satcompute::test::OnlineTestConfiguration;

constexpr int64_t SIMULATION_DURATION_NS = 100000000;

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

NetworkTransfer
MakeTransfer(uint64_t transferId,
             uint32_t sourceSatelliteId,
             uint32_t destinationSatelliteId,
             int64_t arrivalTimeNs)
{
    NetworkTransfer transfer;
    transfer.transferId = transferId;
    transfer.sourceSatelliteId = sourceSatelliteId;
    transfer.destinationSatelliteId = destinationSatelliteId;
    transfer.sizeBytes = 1000000;
    transfer.arrivalTimeNs = arrivalTimeNs;
    return transfer;
}

const TransferSummaryRecord&
FindSummary(const std::vector<TransferSummaryRecord>& summaries, uint64_t transferId)
{
    const auto summary = std::find_if(summaries.begin(),
                                      summaries.end(),
                                      [transferId](const TransferSummaryRecord& candidate) {
                                          return candidate.transferId == transferId;
                                      });
    Check(summary != summaries.end(), "missing transfer lifecycle summary");
    return *summary;
}

class CompletionRecorder
{
  public:
    void
    Record(uint64_t, int64_t)
    {
        ++count;
    }

    uint32_t count{};
};

void
ConfigureEngine(Ptr<NetworkTransferEngine> engine,
                OnlineTopologyController& topology,
                const SatComputeConfig& config)
{
    engine->Configure(topology,
                      "fixed",
                      1024,
                      config.islMtuBytes,
                      config.receiverRcvBufBytes,
                      false,
                      SIMULATION_DURATION_NS);
}

void
RunSizeAwareTerminalCase()
{
    OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                           8,
                                                           "fixed",
                                                           SIMULATION_DURATION_NS,
                                                           1000000000,
                                                           6171353.0L);
    config.parameters.routingMode = "global-size-aware-hrw";
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        ConfigureEngine(engine, topology, config.parameters);
        engine->RegisterPlans({MakeTransfer(1, 0, 3, 1),
                               MakeTransfer(2, 1, 4, -1)});

        CompletionRecorder recorder;
        bool firstFailure = false;
        bool repeatedFailure = true;
        bool firstCancellation = false;
        bool repeatedCancellation = true;
        Simulator::Schedule(NanoSeconds(1), [engine, &recorder] {
            engine->StartTransferNow(
                1,
                MakeCallback(&CompletionRecorder::Record, &recorder));
        });
        Simulator::Schedule(NanoSeconds(2), [&] {
            firstFailure = engine->FinalizeTransferIfActive(
                1,
                TransferTerminalState::FAILED,
                TransferTerminalReason::SOURCE_SATELLITE_FAILED);
            repeatedFailure = engine->FinalizeTransferIfActive(
                1,
                TransferTerminalState::FAILED,
                TransferTerminalReason::SOURCE_SATELLITE_FAILED);
            firstCancellation = engine->FinalizeTransferIfActive(
                2,
                TransferTerminalState::CANCELLED,
                TransferTerminalReason::TASK_FAILED);
            repeatedCancellation = engine->FinalizeTransferIfActive(
                2,
                TransferTerminalState::CANCELLED,
                TransferTerminalReason::TASK_FAILED);
        });
        Simulator::Stop(NanoSeconds(50000000));
        Simulator::Run();

        Check(firstFailure && !repeatedFailure && firstCancellation &&
                  !repeatedCancellation,
              "unified transfer finalization is not idempotent");
        Check(recorder.count == 0,
              "terminal transfer invoked its cleared completion callback");
        Check(engine->GetTransferState(1) == TransferRuntimeState::FAILED &&
                  engine->GetTerminalReason(1) ==
                      TransferTerminalReason::SOURCE_SATELLITE_FAILED &&
                  engine->GetTerminalTimeNs(1) == 2,
              "failed transfer terminal record differs");
        Check(engine->GetTransferState(2) == TransferRuntimeState::CANCELLED &&
                  engine->GetTerminalReason(2) == TransferTerminalReason::TASK_FAILED &&
                  engine->GetTerminalTimeNs(2) == 2,
              "registered cancellation terminal record differs");
        Check(engine->GetStalePacketCount(1) > 0,
              "late packet was not isolated as stale after transfer failure");

        const std::vector<TransferSummaryRecord> summaries = engine->CollectSummaries();
        const TransferSummaryRecord& failed = FindSummary(summaries, 1);
        const TransferSummaryRecord& cancelled = FindSummary(summaries, 2);
        Check(failed.sentApplicationBytes == 1024 &&
                  failed.receivedApplicationBytes == 0 &&
                  failed.transferState == "FAILED" &&
                  failed.terminalReason == "SOURCE_SATELLITE_FAILED" &&
                  failed.stalePacketCount > 0,
              "failed transfer did not preserve bounded send/receive history");
        Check(cancelled.sentApplicationBytes == 0 &&
                  cancelled.receivedApplicationBytes == 0 &&
                  cancelled.transferState == "CANCELLED" &&
                  cancelled.terminalReason == "TASK_FAILED",
              "cancelled registered transfer summary differs");

        Ptr<FlowRouteRegistry> registry = topology.GetFlowRouteRegistry();
        Check(registry != nullptr && registry->GetActiveFlowCount() == 0 &&
                  registry->GetAssignmentCount() == 0 &&
                  registry->GetTotalReservedBytes() == 0,
              "size-aware flow resources leaked after terminal cleanup");
        const auto& events = registry->GetEvents();
        Check(std::any_of(events.begin(),
                          events.end(),
                          [](const FlowRouteReservationEvent& event) {
                              return event.action == "RELEASE_TRANSFER_FAILED";
                          }),
              "failed flow emitted no assignment-release evidence");
    }
    ResetSimulationGlobals();
}

void
RunCapacityActiveTerminalCase()
{
    OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                           8,
                                                           "fixed",
                                                           SIMULATION_DURATION_NS,
                                                           1000000000,
                                                           6171353.0L);
    config.parameters.routingMode = "global-capacity-aware-hrw";
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        ConfigureEngine(engine, topology, config.parameters);
        engine->RegisterPlans({MakeTransfer(3, 0, 3, 1)});

        Simulator::Schedule(NanoSeconds(1), [engine] {
            engine->StartTransferNow(3);
            Check(engine->GetTransferState(3) == TransferRuntimeState::ACTIVE,
                  "capacity-aware transfer was not admitted");
            const CapacityAwareRuntimeSummary active =
                engine->CollectCapacityAwareSummary();
            Check(active.activePathCountAtEnd == 1 &&
                      active.totalReservedRateBpsAtEnd > 0,
                  "capacity-aware path was not reserved before failure");
        });
        Simulator::Schedule(NanoSeconds(2), [engine] {
            Check(engine->FinalizeTransferIfActive(
                      3,
                      TransferTerminalState::FAILED,
                      TransferTerminalReason::DESTINATION_SATELLITE_FAILED),
                  "active capacity-aware transfer did not fail");
        });
        Simulator::Stop(NanoSeconds(50000000));
        Simulator::Run();

        const CapacityAwareRuntimeSummary terminal =
            engine->CollectCapacityAwareSummary();
        Check(terminal.activePathCountAtEnd == 0 &&
                  terminal.reservedDirectedLinkCountAtEnd == 0 &&
                  terminal.totalReservedRateBpsAtEnd == 0 &&
                  terminal.pendingTransferCountAtEnd == 0,
              "capacity-aware path or rate leaked after failure");
        Ptr<FlowRouteRegistry> registry = topology.GetFlowRouteRegistry();
        Check(registry->GetActiveFlowCount() == 0 &&
                  registry->GetAssignmentCount() == 0 &&
                  registry->GetTotalReservedBytes() == 0,
              "capacity-aware flow assignments leaked after failure");
    }
    ResetSimulationGlobals();
}

void
RunCapacityPendingTerminalCase()
{
    OnlineTestConfiguration config = MakeOnlineTestConfig(2,
                                                           8,
                                                           "fixed",
                                                           SIMULATION_DURATION_NS,
                                                           1000000000,
                                                           1.0L);
    config.parameters.routingMode = "global-capacity-aware-hrw";
    {
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        Ptr<NetworkTransferEngine> engine = CreateObject<NetworkTransferEngine>();
        ConfigureEngine(engine, topology, config.parameters);
        engine->RegisterPlans({MakeTransfer(4, 0, 3, 1)});

        Simulator::Schedule(NanoSeconds(1), [engine] {
            engine->StartTransferNow(4);
            Check(engine->GetTransferState(4) ==
                      TransferRuntimeState::WAITING_ADMISSION &&
                      engine->CollectCapacityAwareSummary().pendingTransferCountAtEnd == 1,
                  "disconnected capacity-aware transfer did not remain pending");
        });
        Simulator::Schedule(NanoSeconds(2), [engine] {
            Check(engine->FinalizeTransferIfActive(
                      4,
                      TransferTerminalState::CANCELLED,
                      TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER),
                  "pending capacity-aware transfer did not cancel");
            Check(!engine->FinalizeTransferIfActive(
                       4,
                       TransferTerminalState::CANCELLED,
                       TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER),
                  "pending capacity-aware cancellation was not idempotent");
        });
        Simulator::Stop(NanoSeconds(10));
        Simulator::Run();

        const CapacityAwareRuntimeSummary terminal =
            engine->CollectCapacityAwareSummary();
        Check(terminal.activePathCountAtEnd == 0 &&
                  terminal.totalReservedRateBpsAtEnd == 0 &&
                  terminal.pendingTransferCountAtEnd == 0,
              "pending admission leaked after cancellation");
        Check(engine->GetCapacityWaitingTimeNs(4) == 1,
              "pending transfer did not preserve its capacity wait history");
        const std::vector<TransferSummaryRecord> summaries = engine->CollectSummaries();
        Check(FindSummary(summaries, 4).sentApplicationBytes == 0,
              "never-admitted transfer injected application bytes");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main()
{
    try
    {
        RunSizeAwareTerminalCase();
        RunCapacityActiveTerminalCase();
        RunCapacityPendingTerminalCase();
        std::cout << "SatCompute fault-safe lifecycle tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
