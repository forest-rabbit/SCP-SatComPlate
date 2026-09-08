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
Run(bool queryEnabled, bool auditEnabled)
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
        parameters.f1.temperature.heatingTauSeconds = 2.0;
        parameters.f2.enabled = true;
        parameters.f3.enabled = true;
        model->Configure(parameters, ids, ids, durationNs, controller, auditEnabled);
        Check(model->QueryComputeRisk(3).status == ComputeRiskStatus::NOT_READY,
              "unbound query must not be ready");
        model->BindOrbitConstellation(topology.GetConstellation());
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
                    Check(q == model->QueryComputeRisk(live.nodeId), "query is not idempotent");
                    Check(q.asOfTimeNs == second * 1000000000LL && q.horizonNs == 1000000000LL,
                          "query time/horizon differs");
                    if (q.status == ComputeRiskStatus::AVAILABLE)
                    {
                        Check(q.checkCount == 1 && q.pF1 && q.pF2 && q.pCompute,
                              "available query lacks one-second probabilities");
                        Close(*q.pCompute, CombineComputeFaultProbabilities(*q.pF1, *q.pF2));
                        auto f1 = live.f1State;
                        auto f2 = live.f2State;
                        F1SelfStateFaultModel(parameters.f1).Update(f1, isBusy, 1.0);
                        F2RadiationFaultModel(parameters.f2)
                            .Update(f2,
                                    topology.GetConstellation().GetPositionAt(live.nodeId,
                                                                              Seconds(second + 1)),
                                    1.0);
                        Close(*q.pF1, f1.stepFailureProbability);
                        Close(*q.pF2, f2.stepFailureProbability);
                        if (isBusy)
                            ++busy;
                        else if (!live.riskEpisodeActive)
                            ++idle;
                        auto longer = model->QueryComputeRisk(live.nodeId, 2000000000LL);
                        Check(longer.checkCount == 2 && *longer.pCompute >= *q.pCompute,
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
                              before[i].riskEpisodeActive == after[i].riskEpisodeActive &&
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
} // namespace

int
main()
{
    try
    {
        const auto control = Run(false, false);
        const auto queried = Run(true, false);
        const auto audited = Run(true, true);
        Check(control.business == queried.business && queried.business == audited.business,
              "query/audit changed generation");
        Check(queried.queries == audited.queries, "audit changed query values");
        std::cout << "SatCompute read-only node risk query tests passed.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
