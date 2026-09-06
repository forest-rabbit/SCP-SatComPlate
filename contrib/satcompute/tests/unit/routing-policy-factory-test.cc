/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/capacity-aware-hrw-policy.h"
#include "ns3/capacity-reservation-state.h"
#include "ns3/flow-route-registry.h"
#include "ns3/global-first-policy.h"
#include "ns3/hash-per-flow-policy.h"
#include "ns3/hrw-per-flow-policy.h"
#include "ns3/routing-policy-factory.h"
#include "ns3/size-aware-hrw-policy.h"

#include <iostream>
#include <stdexcept>
#include <string>
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

class EmptyPathView : public CapacityAwarePathView
{
  public:
    std::vector<EcmpRouteCandidate>
    GetEcmpRouteCandidates(uint32_t, uint32_t) const override
    {
        return {};
    }

    uint32_t
    GetNextHopSatelliteId(uint32_t, uint32_t) const override
    {
        return 0;
    }

    uint64_t
    GetIslDataRateBps(uint32_t, uint32_t) const override
    {
        return 0;
    }
};

void
CheckNextHopPolicies()
{
    Ptr<FlowRouteRegistry> registry = CreateObject<FlowRouteRegistry>();
    const SizeAwareLoadView* loadView = &registry->GetSizeAwareLoadState();

    std::unique_ptr<NextHopPolicy> policy =
        RoutingPolicyFactory::CreateNextHopPolicy(RoutingMode::GLOBAL_FIRST, nullptr, nullptr);
    Check(dynamic_cast<GlobalFirstPolicy*>(policy.get()) != nullptr,
          "global-first factory product differs");

    policy = RoutingPolicyFactory::CreateNextHopPolicy(
        RoutingMode::HASH_PER_FLOW,
        nullptr,
        nullptr);
    Check(dynamic_cast<HashPerFlowPolicy*>(policy.get()) != nullptr,
          "hash-per-flow factory product differs");

    policy = RoutingPolicyFactory::CreateNextHopPolicy(
        RoutingMode::HRW_PER_FLOW,
        nullptr,
        nullptr);
    Check(dynamic_cast<HrwPerFlowPolicy*>(policy.get()) != nullptr,
          "HRW-per-flow factory product differs");

    policy = RoutingPolicyFactory::CreateNextHopPolicy(
        RoutingMode::SIZE_AWARE_HRW,
        PeekPointer(registry),
        loadView);
    Check(dynamic_cast<SizeAwareHrwPolicy*>(policy.get()) != nullptr,
          "size-aware factory product differs");

    policy = RoutingPolicyFactory::CreateNextHopPolicy(
        RoutingMode::CAPACITY_AWARE_HRW,
        PeekPointer(registry),
        loadView);
    Check(policy == nullptr, "capacity-aware mode unexpectedly returned a next-hop policy");
}

void
CheckPathPolicies()
{
    EmptyPathView pathView;
    CapacityReservationState reservationState;

    const std::vector<RoutingMode> nextHopModes = {
        RoutingMode::GLOBAL_FIRST,
        RoutingMode::HASH_PER_FLOW,
        RoutingMode::HRW_PER_FLOW,
        RoutingMode::SIZE_AWARE_HRW,
    };
    for (RoutingMode mode : nextHopModes)
    {
        Check(RoutingPolicyFactory::CreatePathPolicy(mode, &pathView, &reservationState) ==
                  nullptr,
              "next-hop routing mode unexpectedly returned a path policy");
    }

    std::unique_ptr<PathPolicy> policy = RoutingPolicyFactory::CreatePathPolicy(
        RoutingMode::CAPACITY_AWARE_HRW,
        &pathView,
        &reservationState);
    Check(dynamic_cast<CapacityAwareHrwPolicy*>(policy.get()) != nullptr,
          "capacity-aware factory product differs");
}

void
ObserveReservation(std::vector<uint64_t>* rates, uint32_t source, uint32_t interface,
                   uint64_t rate)
{
    Check(source == 7 && interface == 2, "reservation observer link identity differs");
    rates->push_back(rate);
}

void
CheckReservationObserver()
{
    CapacityReservationState state;
    CapacityAwarePath path;
    CapacityAwarePathHop hop{};
    hop.sourceSatelliteId = 7;
    hop.destinationSatelliteId = 8;
    hop.candidate.outputInterface = 2;
    hop.linkRateBps = 10'000'000'000;
    path.hops.push_back(hop);
    path.admittedRateBps = 6'000'000'000;
    state.Reserve(1, path);
    std::vector<uint64_t> rates;
    state.SetObserver(MakeBoundCallback(&ObserveReservation, &rates));
    path.admittedRateBps = 4'000'000'000;
    state.Reserve(2, path);
    Check(state.GetResidualRateBps(hop) == 0, "10 Gbps reservation residual differs");
    state.Release(1);
    state.Release(2);
    Check(rates == std::vector<uint64_t>({6'000'000'000, 10'000'000'000,
                                         4'000'000'000, 0}),
          "reservation observer events differ");
    Check(state.GetResidualRateBps(hop) == hop.linkRateBps,
          "observer changed reservation release");
    state.SetObserver({});
    state.Reserve(3, path);
    state.Release(3);
    Check(rates.size() == 4, "disabled observer emitted events");
}

} // namespace

int
main()
{
    try
    {
        CheckNextHopPolicies();
        CheckPathPolicies();
        CheckReservationObserver();
        std::cout << "SatCompute routing policy factory tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
