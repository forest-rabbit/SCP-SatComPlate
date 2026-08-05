/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/para.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{

void
Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

} // namespace

int
main()
{
    try
    {
        const ns3::SatComputeConfig config = ns3::GetDefaultSatComputeConfig();
        Require(config.simulationDurationSeconds == 1000.0, "unexpected duration");
        Require(config.constellationConfig ==
                    "contrib/satcompute/input/topology/constellations/synthetic-66.csv",
                "unexpected constellation path");
        Require(config.maxIslDistanceMeters == 6171353.0, "unexpected ISL distance");
        Require(config.delayMode == "fixed" && config.fixedDelaySeconds == 0.008,
                "unexpected delay defaults");
        Require(config.networkUpdateIntervalSeconds == 20.0,
                "unexpected network interval");
        Require(config.islBandwidthBps == 2'000'000'000 &&
                    config.islMtuBytes == 64'028 &&
                    config.islQueueBytes == 1'500'000 &&
                    config.receiverRcvBufBytes == 131'072,
                "unexpected link defaults");
        Require(config.routingMode == "global-capacity-aware-hrw" &&
                    config.ecmpHashSeed == 1,
                "unexpected routing defaults");
        Require(config.computeProfile.empty() && config.taskTrace.empty(),
                "workload inputs must be opt-in");
        Require(config.transferChunkMode == "size-aware" &&
                    config.transferPayloadBytes == 1'024 &&
                    config.taskCompletionPolicy == "strict",
                "unexpected workload defaults");
        Require(!config.topologyOnly && config.topologySliceIntervalSeconds == 1.0 &&
                    config.includeFinalTopologyState,
                "unexpected topology-only defaults");
        Require(config.outputDirectory == "/tmp/satcompute-output" &&
                    config.taskLogMode == "summary" && config.diagnosticMode == "off",
                "unexpected output defaults");
        Require(config.randomSeed == 1 && config.randomRun == 1,
                "unexpected random defaults");
    }
    catch (const std::exception& error)
    {
        std::cerr << "para defaults test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
