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

        Require(config.runName == "synthetic-66-fixed", "unexpected run name");
        Require(config.simulationStartSeconds == 0.0, "unexpected start time");
        Require(config.simulationDurationSeconds == 1000.0, "unexpected duration");
        Require(config.constellationConfig ==
                    "contrib/satcompute/input/topology/constellations/synthetic-66.json",
                "unexpected constellation path");
        Require(config.topologySource == "online", "unexpected topology source");
        Require(config.topologyDirectory.empty(), "online replay directory must be empty");
        Require(config.islCandidateStrategy == "plus-grid", "unexpected ISL strategy");
        Require(!config.seamEnabled, "seam must be disabled by default");
        Require(config.maxIslDistanceMeters == 6174589.0, "unexpected ISL distance");
        Require(config.delayMode == "fixed", "unexpected delay mode");
        Require(config.fixedDelaySeconds == 0.008, "unexpected fixed delay");
        Require(config.networkUpdateIntervalSeconds == 20.0,
                "unexpected network interval");
        Require(config.islBandwidthBps == 2000000000ULL, "unexpected ISL bandwidth");
        Require(config.islMtuBytes == 1500, "unexpected ISL MTU");
        Require(config.islQueueBytes == 1500000, "unexpected ISL queue");
        Require(config.receiverRcvBufBytes == 131072, "unexpected receive buffer");
        Require(config.routingMode == "global-capacity-aware-hrw", "unexpected route mode");
        Require(config.routingRecomputePolicy == "on-topology-change",
                "unexpected recompute policy");
        Require(config.ecmpHashSeed == 1, "unexpected ECMP seed");
        Require(config.transferTrace.empty() && config.computeProfile.empty() &&
                    config.taskTrace.empty(),
                "workload inputs must be opt-in");
        Require(config.transferChunkMode == "fixed", "unexpected chunk mode");
        Require(config.transferPayloadBytes == 1024, "unexpected transfer payload");
        Require(config.taskCompletionPolicy == "strict", "unexpected completion policy");
        Require(config.topologyExportEnabled, "topology export must be enabled");
        Require(config.topologyExportIntervalSeconds == 1.0,
                "unexpected export interval");
        Require(config.includeFinalTopologyState, "final topology state must be exported");
        Require(config.outputDirectory == "/tmp/satcompute-output", "unexpected output path");
        Require(config.transferLogMode == "summary", "unexpected transfer log mode");
        Require(config.taskLogMode == "summary", "unexpected task log mode");
        Require(config.diagnosticMode == "off", "unexpected diagnostic mode");
        Require(config.randomSeed == 1 && config.randomRun == 1 &&
                    config.randomStreamStart == 0,
                "unexpected random defaults");
    }
    catch (const std::exception& error)
    {
        std::cerr << "para test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
