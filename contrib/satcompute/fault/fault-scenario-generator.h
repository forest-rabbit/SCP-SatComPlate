/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_SCENARIO_GENERATOR_H
#define SATCOMPUTE_FAULT_SCENARIO_GENERATOR_H

#include "fault-controller.h"
#include "fault-model-config.h"
#include "self-state-fault-model.h"

#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ns3
{

class ComputeService;
class TaskCoordinator;

/** Configuration or lifecycle error raised by the online fault generator. */
class FaultScenarioGeneratorError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** Observable end-of-run F1 state for one configured compute node. */
struct FaultGeneratorNodeSnapshot
{
    uint32_t nodeId{};
    SelfStateFaultSnapshot selfState;
    bool riskEpisodeActive{};
    bool computeAvailable{true};
};

/** Generate unified fault events online while reusing the N4A controller. */
class FaultScenarioGenerator : public Object
{
  public:
    static TypeId GetTypeId();

    FaultScenarioGenerator();
    ~FaultScenarioGenerator() override;

    /**
     * Configure model state and deterministic per-node random streams at time zero.
     *
     * @param config Strictly validated unified fault-model configuration.
     * @param computeNodeIds Stable IDs present in the compute profile.
     * @param simulationDurationNs Exclusive simulation end in nanoseconds.
     * @param faultController N4A controller configured for online generation.
     */
    void Configure(const FaultModelConfig& config,
                   const std::vector<uint32_t>& computeNodeIds,
                   int64_t simulationDurationNs,
                   Ptr<FaultController> faultController);

    /** Bind live compute services after TaskCoordinator initialization. */
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    /** Close open risk episodes and return the canonical generated trace. */
    const FaultTrace& Finalize();

    /** Return node snapshots in ascending stable-node-ID order. */
    std::vector<FaultGeneratorNodeSnapshot> GetNodeSnapshots() const;

  private:
    struct RiskEpisode
    {
        uint64_t faultId{};
        int64_t noticeTimeNs{};
        double noticeProbability{};
    };

    struct NodeState
    {
        SelfStateFaultSnapshot selfState;
        std::optional<RiskEpisode> riskEpisode;
        Ptr<UniformRandomVariable> random;
        Ptr<ComputeService> computeService;
    };

    FaultDefinition MakeNotice(uint32_t nodeId,
                               const RiskEpisode& episode) const;
    FaultDefinition MakeRiskOnly(uint32_t nodeId,
                                 const RiskEpisode& episode,
                                 int64_t clearTimeNs) const;
    FaultDefinition MakeComputeFault(uint32_t nodeId,
                                     uint64_t faultId,
                                     const std::optional<RiskEpisode>& episode,
                                     double currentProbability,
                                     int64_t startTimeNs) const;
    void Tick(int64_t simulationTimeNs);
    void DoDispose() override;

    bool m_configured{};
    bool m_bound{};
    bool m_finalized{};
    int64_t m_simulationDurationNs{};
    FaultModelConfig m_config;
    std::optional<SelfStateFaultModel> m_selfStateModel;
    std::map<uint32_t, NodeState> m_nodes;
    uint64_t m_nextFaultId{1};
    FaultTrace m_trace;
    std::vector<EventId> m_tickEvents;
    Ptr<FaultController> m_faultController;
    Ptr<TaskCoordinator> m_taskCoordinator;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_SCENARIO_GENERATOR_H
