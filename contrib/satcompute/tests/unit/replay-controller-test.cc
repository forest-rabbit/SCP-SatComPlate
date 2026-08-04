/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/channel.h"
#include "ns3/command-line.h"
#include "ns3/data-rate.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/replay-topology-controller.h"
#include "ns3/scenario-config.h"
#include "ns3/simulator.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

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
ExpectControllerError(const std::function<void()>& operation,
                      const std::string& message)
{
    try
    {
        operation();
    }
    catch (const ReplayTopologyControllerError&)
    {
        return;
    }
    throw std::runtime_error(message);
}

Ptr<PointToPointNetDevice>
GetFirstDevice(const ReplayTopologyController& controller)
{
    const NetDeviceContainer devices =
        controller.GetLinkState().GetLinkDevices(0, 1);
    const Ptr<PointToPointNetDevice> device =
        DynamicCast<PointToPointNetDevice>(devices.Get(0));
    Check(device != nullptr, "replay ISL device type differs");
    return device;
}

int64_t
GetDelayNs(const ReplayTopologyController& controller)
{
    TimeValue delay;
    GetFirstDevice(controller)->GetChannel()->GetAttribute("Delay", delay);
    return delay.Get().GetNanoSeconds();
}

uint64_t
GetDataRateBps(const ReplayTopologyController& controller)
{
    DataRateValue dataRate;
    GetFirstDevice(controller)->GetAttribute("DataRate", dataRate);
    return dataRate.Get().GetBitRate();
}

void
ResetSimulationGlobals()
{
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}

void
RunDelayCase(const std::string& scenarioFilename,
             int64_t expectedInitialDelayNs,
             int64_t expectedFinalDelayNs,
             uint32_t expectedReconfiguredLinks)
{
    {
        const ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        ReplayTopologyController controller(config);
        ExpectControllerError(
            [&controller] { controller.GetNodes(); },
            "uninitialized replay controller exposed nodes");
        controller.Initialize();

        Check(controller.GetAppliedSnapshotCount() == 1,
              "initial replay snapshot count differs");
        Check(controller.GetRouteComputationCount() == 1,
              "initial route computation count differs");
        Check(controller.GetServiceAddress(0) == Ipv4Address("172.16.0.1"),
              "replay service address differs");
        Check(GetDelayNs(controller) == expectedInitialDelayNs,
              "initial replay delay differs");
        Check(GetDataRateBps(controller) == config.network.linkBandwidthBps,
              "scenario link bandwidth did not override replay metadata");

        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(controller.GetAppliedSnapshotCount() == 2,
              "periodic replay snapshot count differs");
        Check(controller.GetRouteComputationCount() == 1,
              "delay-only replay rebuilt IPv4 routes");
        Check(!controller.GetLastUpdateSummary().ActiveEdgeSetChanged(),
              "delay-only replay changed active edges");
        Check(controller.GetLastUpdateSummary().reconfiguredLinks ==
                  expectedReconfiguredLinks,
              "delay-only reconfiguration count differs");
        Check(GetDelayNs(controller) == expectedFinalDelayNs,
              "final replay delay differs");
    }
    ResetSimulationGlobals();
}

void
RunDynamicCase(const std::string& scenarioFilename)
{
    {
        const ScenarioConfig config = LoadScenarioConfig(scenarioFilename);
        ReplayTopologyController controller(config);
        controller.Initialize();
        Check(controller.GetRouteComputationCount() == 1,
              "dynamic replay initial route count differs");

        Simulator::Stop(NanoSeconds(config.simulation.durationNs));
        Simulator::Run();

        Check(controller.GetAppliedSnapshotCount() == 3,
              "dynamic replay snapshot count differs");
        Check(controller.GetRouteComputationCount() == 3,
              "dynamic replay did not rebuild routes exactly once per edge change");
        Check(controller.GetLastUpdateSummary().reenabledLinks == 1,
              "dynamic replay did not restore the expected edge");
    }
    ResetSimulationGlobals();
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string fixedScenario;
    std::string distanceScenario;
    std::string dynamicScenario;
    CommandLine command(__FILE__);
    command.AddValue("fixedScenario", "Fixed-delay replay scenario", fixedScenario);
    command.AddValue("distanceScenario", "Distance-delay replay scenario", distanceScenario);
    command.AddValue("dynamicScenario", "Dynamic-edge replay scenario", dynamicScenario);
    command.Parse(argc, argv);

    try
    {
        Check(!fixedScenario.empty(), "fixedScenario is required");
        Check(!distanceScenario.empty(), "distanceScenario is required");
        Check(!dynamicScenario.empty(), "dynamicScenario is required");

        ScenarioConfig unsupported = LoadScenarioConfig(dynamicScenario);
        unsupported.routing.mode = "global-size-aware-hrw";
        ExpectControllerError(
            [&unsupported] { ReplayTopologyController controller(unsupported); },
            "unmigrated routing mode was silently run as global-first");

        RunDelayCase(fixedScenario, 8000000, 8000000, 0);
        RunDelayCase(distanceScenario, 1000000, 1500000, 1);
        RunDynamicCase(dynamicScenario);

        std::cout << "SatCompute replay topology controller tests passed."
                  << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        ResetSimulationGlobals();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
