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
        Require(config.simulationDurationSeconds == 1300.0, "unexpected duration");
        Require(config.constellationConfig ==
                    "contrib/satcompute/input/topology/constellations/synthetic-66.csv",
                "unexpected constellation path");
        Require(config.orbitStartOffsetSeconds == 0.0,
                "unexpected orbit start offset");
        Require(config.maxIslDistanceMeters == 6171353.0, "unexpected ISL distance");
        Require(config.delayMode == "fixed" && config.fixedDelaySeconds == 0.001,
                "unexpected delay defaults");
        Require(config.networkUpdateIntervalSeconds == 20.0,
                "unexpected network interval");
        Require(config.islBandwidthBps == 10'000'000'000 &&
                    config.islMtuBytes == 64'028 &&
                    config.islQueueBytes == 1'500'000 &&
                    config.receiverRcvBufBytes == 131'072,
                "unexpected link defaults");
        Require(config.routingMode == "global-capacity-aware-hrw" &&
                    config.ecmpHashSeed == 1,
                "unexpected routing defaults");
        Require(config.computeProfile ==
                    "contrib/satcompute/input/examples/leo-66-1300s-n4c-g3-truncnormal-v3/compute-profile.json" &&
                    config.taskTrace ==
                    "contrib/satcompute/input/examples/leo-66-1300s-n4c-g3-truncnormal-v3/task-trace.json",
                "workload inputs must match the frozen scene");
        Require(config.computeDeadlineFactor == 1.3, "unexpected compute deadline factor");
        Require(config.transferChunkMode == "size-aware" &&
                    config.transferPayloadBytes == 1'024 &&
                    config.taskCompletionPolicy == "report",
                "unexpected workload defaults");
        Require(config.faultMode == "generate" && config.faultTrace.empty(),
                "generate must derive its output path from outputDir");
        Require(!config.faultProbabilityAudit,
                "fault probability audit must be opt-in");
        Require(!config.compfrrShadow && config.compfrrShadowOutput.empty(),
                "shadow evaluation must be opt-in");
        Require(!config.topologyOnly && config.topologySliceIntervalSeconds == 1.0 &&
                    config.includeFinalTopologyState,
                "unexpected topology-only defaults");
        Require(config.outputDirectory == "/tmp/satcompute-output" &&
                    config.taskLogMode == "summary" && config.diagnosticMode == "off",
                "unexpected output defaults");
        Require(config.randomSeed == 1 && config.randomRun == 11,
                "unexpected random defaults");
        Require(config.linkMetrics && config.linkMetricsIntervalSeconds == 1.0,
                "frozen scene must collect one-second link metrics");
    }
    catch (const std::exception& error)
    {
        std::cerr << "para defaults test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
