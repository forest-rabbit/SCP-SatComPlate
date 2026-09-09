/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "ns3/fault-model-engine.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/task-coordinator.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace ns3;

namespace
{
void
Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

void
Close(double a, double b)
{
    Check(std::abs(a - b) < 1e-12, "query probability disagrees with live/pure model");
}

struct Signature
{
    std::vector<std::string> business;
    std::vector<ComputeRiskSnapshot> queries;
};

Signature
Run(bool queryEnabled, bool auditEnabled, bool computeSources = true, bool controlledF3 = false)
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(1);
    constexpr int64_t durationNs = 30000000000LL;
    Signature signature;
    {
        auto config = satcompute::test::MakeOnlineTestConfig(2,
                                                             8,
                                                             "fixed",
                                                             durationNs,
                                                             20000000000LL,
                                                             6171353.0L);
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        ComputeProfile profile;
        for (uint32_t id : ids)
            profile.nodes.push_back({id, 1000});
        TaskTrace tasks;
        tasks.tasks.push_back({1, 0, 3, 0, 1, 1, 100000, 0, 1, 2});
        auto controller = CreateObject<FaultController>();
        controller->ConfigureGeneration(ids, durationNs);
        controller->BindTopology(topology);
        auto model = CreateObject<FaultModelEngine>();
        Check(model->QueryComputeRisk(3).status == ComputeRiskStatus::NOT_READY,
              "unconfigured query must not be ready");
        auto parameters = GetDefaultFaultParameters();
        parameters.f1.temperature.heatingToCriticalSeconds = 2.0;
        parameters.f1.enabled = computeSources;
        parameters.f2.enabled = computeSources;
        parameters.f3.enabled = true;
        if (controlledF3)
        {
            parameters.f3.mode = "controlled";
            parameters.f3.controlledNodeId = 3;
            parameters.f3.controlledStartSeconds = 2.0;
        }
        model->Configure(parameters, ids, ids, durationNs, controller, auditEnabled);
        Check(model->QueryComputeRisk(3).status == ComputeRiskStatus::NOT_READY,
              "unbound query must not be ready");
        if (computeSources)
        {
            model->BindOrbitConstellation(topology.GetConstellation());
        }
        auto coordinator = CreateObject<TaskCoordinator>();
        coordinator->Initialize(profile,
                                tasks,
                                topology,
                                "fixed",
                                1024,
                                config.parameters.islMtuBytes,
                                config.parameters.receiverRcvBufBytes,
                                false,
                                durationNs);
        controller->BindTaskCoordinator(coordinator);
        model->BindTaskCoordinator(coordinator);
        if (queryEnabled)
        {
            Check(model->QueryComputeRisk(9999).status == ComputeRiskStatus::NOT_READY,
                  "unknown query must not be ready");
            for (int64_t bad : {int64_t(0), int64_t(-1)})
            {
                bool threw = false;
                try
                {
                    model->QueryComputeRisk(3, bad);
                }
                catch (const FaultModelEngineError&)
                {
                    threw = true;
                }
                Check(threw, "invalid risk horizon accepted");
            }
        }
        std::map<uint32_t, ComputeRiskSnapshot> previous;
        std::map<uint32_t, bool> previousBusy;
        unsigned matched = 0, idle = 0, busy = 0, unavailable = 0, permanent = 0;
        for (int second = 0; second < 30 && queryEnabled; ++second)
        {
            Simulator::Schedule(Seconds(second), [&, second] {
                const auto before = model->GetNodeSnapshots();
                const auto eventCount = controller->GetEvents().size();
                for (const auto& live : before)
                {
                    const auto service = coordinator->GetComputeServices().at(live.nodeId);
                    const bool isBusy = service->HasRunningTask();
                    const auto old = previous.find(live.nodeId);
                    if (old != previous.end() &&
                        old->second.status == ComputeRiskStatus::AVAILABLE &&
                        previousBusy.at(live.nodeId) == isBusy &&
                        controller->GetState().IsSatelliteAvailable(live.nodeId))
                    {
                        Close(*old->second.pF1, live.f1State.stepFailureProbability);
                        Close(*old->second.pF2, live.f2State.stepFailureProbability);
                        Close(*old->second.pCompute, live.combinedStepFailureProbability);
                        ++matched;
                    }
                    const auto q = model->QueryComputeRisk(live.nodeId);
                    if (controlledF3 && second < 2)
                    {
                        Check(!q.permanentlyUnavailable && q.status == ComputeRiskStatus::AVAILABLE,
                              "controlled F3 truth leaked before actual failure");
                    }
                    Check(q == model->QueryComputeRisk(live.nodeId), "query is not idempotent");
                    Check(q.asOfTimeNs == second * 1000000000LL && q.horizonNs == 1000000000LL,
                          "query time/horizon differs");
                    if (q.status == ComputeRiskStatus::AVAILABLE)
                    {
                        Check(q.checkCount == (computeSources ? 1 : 0) && q.pF1 && q.pF2 &&
                                  q.pCompute,
                              "available query lacks one-second probabilities");
                        Close(*q.pCompute, CombineComputeFaultProbabilities(*q.pF1, *q.pF2));
                        auto f1 = live.f1State;
                        auto f2 = live.f2State;
                        if (computeSources)
                        {
                            F1SelfStateFaultModel(parameters.f1).Update(f1, isBusy, 1.0);
                            F2RadiationFaultModel(parameters.f2)
                                .Update(
                                    f2,
                                    topology.GetConstellation().GetPositionAt(live.nodeId,
                                                                              Seconds(second + 1)),
                                    1.0);
                        }
                        Close(*q.pF1, f1.stepFailureProbability);
                        Close(*q.pF2, f2.stepFailureProbability);
                        if (isBusy)
                            ++busy;
                        else
                            ++idle;
                        auto longer = model->QueryComputeRisk(live.nodeId, 2000000000LL);
                        Check(longer.checkCount == (computeSources ? 2 : 0) &&
                                  *longer.pCompute >= *q.pCompute,
                              "longer horizon is not cumulative");
                        auto shorter = model->QueryComputeRisk(live.nodeId, 1);
                        Check(shorter.checkCount == 0 && shorter.pCompute == 0.0,
                              "query invented a fault check outside the discrete grid");
                    }
                    else
                    {
                        Check(q.status == ComputeRiskStatus::UNAVAILABLE && !q.pCompute && !q.pF1 &&
                                  !q.pF2,
                              "failed node was represented by zero risk");
                        ++unavailable;
                        permanent += q.permanentlyUnavailable;
                    }
                    signature.queries.push_back(q);
                    previous[live.nodeId] = q;
                    previousBusy[live.nodeId] = isBusy;
                }
                const auto after = model->GetNodeSnapshots();
                for (std::size_t i = 0; i < before.size(); ++i)
                {
                    Check(before[i].f1SampleCount == after[i].f1SampleCount &&
                              before[i].f2SampleCount == after[i].f2SampleCount &&
                              before[i].f1State.temperatureC == after[i].f1State.temperatureC &&
                              before[i].combinedStepFailureProbability ==
                                  after[i].combinedStepFailureProbability,
                          "query changed actual state or RNG");
                }
                Check(controller->GetEvents().size() == eventCount, "query created fault events");
                if (second == 1)
                {
                    bool threw = false;
                    try
                    {
                        model->QueryComputeRisk(3, std::numeric_limits<int64_t>::max());
                    }
                    catch (const FaultModelEngineError&)
                    {
                        threw = true;
                    }
                    Check(threw, "overflowing query accepted");
                }
            });
        }
        Simulator::Stop(NanoSeconds(durationNs));
        Simulator::Run();
        model->Finalize();
        Check(model->QueryComputeRisk(3).status == ComputeRiskStatus::NOT_READY,
              "finalized model still ready");
        if (queryEnabled)
            Check(matched > 0 && idle > 0 && busy > 0 && unavailable > 0 && permanent > 0,
                  "risk query scenarios were not exercised");
        for (const auto& event : controller->GetEvents())
        {
            std::ostringstream s;
            s << event.simulationTimeNs << ':' << event.nodeId << ':' << int(event.eventType) << ':'
              << event.affectedTaskCount << ':' << event.affectedTransferCount;
            signature.business.push_back(s.str());
        }
        for (const auto& state : model->GetNodeSnapshots())
        {
            std::ostringstream s;
            s << state.nodeId << ':' << state.f1SampleCount << ':' << state.f2SampleCount << ':'
              << state.f1State.temperatureC << ':' << state.f1OccurrenceCount << ':'
              << state.f2OccurrenceCount;
            signature.business.push_back(s.str());
        }
    }
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
    return signature;
}
// Runtime boundaries: fractional thermal recovery, joint-source recovery, and F3 preemption.
void
RunRecoveryBoundary(bool joint, bool preempt)
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(1);
    {
        constexpr int64_t durationNs = 60000000000LL;
        auto config = satcompute::test::MakeOnlineTestConfig(2, 8, "fixed", durationNs,
                                                            20000000000LL, 6171353.0L);
        OnlineTopologyController topology(config.parameters, config.constellation);
        topology.Initialize();
        const auto ids = topology.GetIdMap().GetCanonicalSatelliteIds();
        ComputeProfile profile;
        for (auto id : ids)
            profile.nodes.push_back({id, 1000});
        TaskTrace tasks;
        tasks.tasks = {{1, 0, 3, 0, 1, 1, 100000, 0, 1, 2},
                       {2, 0, 3, 0, 1, 1, 100000, 100000000, 3, 4}};
        auto controller = CreateObject<FaultController>();
        controller->ConfigureGeneration(ids, durationNs);
        controller->BindTopology(topology);
        auto parameters = GetDefaultFaultParameters();
        parameters.f2.enabled = joint;
        parameters.f3.enabled = preempt;
        if (joint)
        {
            // Test-only certainty at t=1, without changing production parameters or RNG.
            parameters.f1.temperature.heatingToCriticalSeconds = 0.01;
            parameters.f2.longitudeMinDegrees = -180;
            parameters.f2.longitudeMaxDegrees = 180;
            parameters.f2.latitudeMinDegrees = -90;
            parameters.f2.latitudeMaxDegrees = 90;
            parameters.f2.sigmaLongitudeWestDegrees = 180;
            parameters.f2.sigmaLongitudeEastDegrees = 181;
            parameters.f2.sigmaLatitudeDegrees = 90;
            parameters.f2.referenceSeuIntensityPerSecond = 1e6;
        }
        if (preempt)
        {
            parameters.f3.mode = "controlled";
            parameters.f3.controlledNodeId = 3;
            parameters.f3.controlledStartSeconds = 3.25;
        }
        auto model = CreateObject<FaultModelEngine>();
        model->Configure(parameters, ids, ids, durationNs, controller, false);
        if (joint)
            model->BindOrbitConstellation(topology.GetConstellation());
        auto coordinator = CreateObject<TaskCoordinator>();
        coordinator->Initialize(profile, tasks, topology, "fixed", 1024,
                                config.parameters.islMtuBytes,
                                config.parameters.receiverRcvBufBytes, false, durationNs);
        controller->BindTaskCoordinator(coordinator);
        model->BindTaskCoordinator(coordinator);
        bool observed = false, recovered = false;
        for (int second = 1; second < 55; ++second)
        {
            Simulator::Schedule(Seconds(second) + NanoSeconds(1), [&] {
                if (observed)
                    return;
                for (const auto& event : controller->GetEvents())
                {
                    if (event.nodeId != 3 || event.faultType != FaultType::COMPUTE ||
                        event.eventType != FaultEventType::START)
                        continue;
                    observed = true;
                    Check(event.affectedTaskCount == 1, "outage must interrupt only RUNNING");
                    if (joint)
                        Check(event.durationNs == 8000000000LL, "joint outage must use max(F1,F2)");
                    else
                        Check(*event.durationNs > 0 && *event.durationNs < 4000000000LL &&
                                  *event.durationNs % 1000000000LL != 0,
                              "fractional F1 recovery was not exercised");
                    const int64_t end = *event.startTimeNs + *event.durationNs;
                    Simulator::Schedule(NanoSeconds(end + 1 - Simulator::Now().GetNanoSeconds()),
                                        [&] {
                        if (preempt)
                        {
                            Check(!controller->GetState().IsSatelliteAvailable(3) &&
                                      !coordinator->GetComputeServices().at(3)->HasRunningTask(),
                                  "old recovery resurrected permanent F3 node");
                        }
                        else
                        {
                            Check(controller->GetState().IsComputeAvailable(3) &&
                                      coordinator->GetComputeServices().at(3)->HasRunningTask(),
                                  "queued task did not resume at exact recovery");
                            for (const auto& state : model->GetNodeSnapshots())
                                if (state.nodeId == 3)
                                    Check(std::abs(state.f1State.temperatureC - 17) < 1e-7,
                                          "recovery did not cool naturally to base");
                        }
                        recovered = true;
                        Simulator::Stop();
                    });
                    break;
                }
            });
        }
        if (joint && !preempt)
            Simulator::Schedule(Seconds(5) + NanoSeconds(1), [&] {
                Check(!controller->GetState().IsComputeAvailable(3),
                      "F1 cooling incorrectly ended joint F2 outage early");
                for (const auto& state : model->GetNodeSnapshots())
                    if (state.nodeId == 3)
                        Close(state.f1State.temperatureC, 17);
            });
        Simulator::Stop(NanoSeconds(durationNs));
        Simulator::Run();
        model->Finalize();
        Check(observed && recovered, "runtime recovery boundary was not exercised");
        for (const auto& fault : controller->GetTrace().faults)
            if (fault.nodeId == 3 && fault.faultType == FaultType::COMPUTE)
            {
                Check(fault.f1Occurred && fault.f2Occurred == joint,
                      "joint source flags lost independent hits");
                if (preempt)
                    Check(fault.GetRecoveryTimeNs() == 3250000000LL,
                          "F3 did not truncate the active compute outage");
            }
    }
    Simulator::Destroy();
    Ipv4AddressGenerator::Reset();
    Mac48Address::ResetAllocationIndex();
}
} // namespace

int
main()
{
    try
    {
        RunRecoveryBoundary(false, false);
        RunRecoveryBoundary(true, false);
        RunRecoveryBoundary(true, true);
        const auto control = Run(false, false);
        const auto queried = Run(true, false);
        const auto audited = Run(true, true);
        Check(control.business == queried.business && queried.business == audited.business,
              "query/audit changed generation");
        Check(queried.queries == audited.queries, "audit changed query values");
        const auto f3Only = Run(true, false, false);
        Check(!f3Only.queries.empty(), "F3-only query case was not exercised");
        const auto controlled = Run(false, false, false, true);
        const auto controlledQueried = Run(true, false, false, true);
        Check(controlled.business == controlledQueried.business,
              "queries altered controlled F3 execution");
        std::cout << "SatCompute read-only node risk query tests passed.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
