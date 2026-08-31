/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_MODEL_ENGINE_H
#define SATCOMPUTE_FAULT_MODEL_ENGINE_H

#include "fault-controller.h"

#include "ns3/compute-failure-probability-record.h"
#include "ns3/fault-para.h"
#include "ns3/compute-fault-combination.h"
#include "ns3/f1-self-state-fault-model.h"
#include "ns3/f2-radiation-fault-model.h"
#include "ns3/f3-debris-fault-model.h"

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
class OnlineOrbitConstellation;
class TaskCoordinator;

/** Configuration or lifecycle error raised by the online fault-model engine. */
class FaultModelEngineError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** Observable end-of-run fault-model state for one configured compute node. */
struct FaultModelNodeSnapshot
{
    uint32_t nodeId{}; ///< Stable external satellite ID.
    F1SelfStateFaultSnapshot f1State; ///< Current pure F1 state.
    F2RadiationFaultSnapshot f2State; ///< Current pure F2 state.
    double combinedStepFailureProbability{}; ///< Current q_comp output.
    uint64_t f1SampleCount{}; ///< Number of independent F1 random draws.
    uint64_t f2SampleCount{}; ///< Number of independent F2 random draws.
    uint64_t f1OccurrenceCount{}; ///< Number of F1 source hits.
    uint64_t f2OccurrenceCount{}; ///< Number of F2 source hits.
    uint64_t computeFaultCount{}; ///< Number of coalesced compute faults.
    bool riskEpisodeActive{}; ///< Whether a notice episode remains open.
    bool computeAvailable{true}; ///< Final N4A compute availability.
};

/** Evaluate fault models online and submit events to the N4A controller. */
class FaultModelEngine : public Object
{
  public:
    /** @return ns-3 runtime type information. */
    static TypeId GetTypeId();

    FaultModelEngine();
    ~FaultModelEngine() override;

    /**
     * Configure model state and deterministic per-node random streams at time zero.
     *
     * @param parameters Strictly validated built-in fault parameters.
     * @param satelliteIds Complete stable-ID constellation universe.
     * @param computeNodeIds Stable IDs present in the compute profile.
     * @param simulationDurationNs Exclusive simulation end in nanoseconds.
     * @param faultController N4A controller configured for online generation.
     * @param probabilityAuditEnabled Whether to collect live probability audit records.
     */
    void Configure(const FaultParameters& parameters,
                   const std::vector<uint32_t>& satelliteIds,
                   const std::vector<uint32_t>& computeNodeIds,
                   int64_t simulationDurationNs,
                   Ptr<FaultController> faultController,
                   bool probabilityAuditEnabled);

    /** Bind live compute services after TaskCoordinator initialization. */
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    /** Bind and initialize the shared native orbit source required by F2. */
    void BindOrbitConstellation(const OnlineOrbitConstellation& constellation);

    /** Close open risk episodes and return the canonical generated trace. */
    const FaultTrace& Finalize();

    /** Return node snapshots in ascending stable-node-ID order. */
    std::vector<FaultModelNodeSnapshot> GetNodeSnapshots() const;

    /** @return Pre-sampling probabilities produced from live generate state. */
    const std::vector<ComputeFailureProbabilityRecord>& GetProbabilityRecords() const;

  private:
    /** Notice metadata retained until risk exit or compute failure. */
    struct RiskEpisode
    {
        uint64_t faultId{}; ///< Stable ID shared by notice and completion record.
        int64_t noticeTimeNs{}; ///< Absolute risk-entry time.
        double noticeProbability{}; ///< Single-step probability visible at notice.
    };

    /** Online model, random stream, and compute-service binding for one node. */
    struct NodeState
    {
        F1SelfStateFaultSnapshot f1State; ///< Current pure F1 state.
        F2RadiationFaultSnapshot f2State; ///< Current pure F2 state.
        std::optional<RiskEpisode> riskEpisode; ///< Open combined-risk episode.
        Ptr<UniformRandomVariable> f1Random; ///< Stable per-node F1 sampling stream.
        Ptr<UniformRandomVariable> f2Random; ///< Stable per-node F2 sampling stream.
        uint64_t f1SampleCount{}; ///< Independent F1 draws consumed.
        uint64_t f2SampleCount{}; ///< Independent F2 draws consumed.
        uint64_t f1OccurrenceCount{}; ///< F1 source hits before coalescing.
        uint64_t f2OccurrenceCount{}; ///< F2 source hits before coalescing.
        uint64_t computeFaultCount{}; ///< Coalesced compute-fault starts.
        std::optional<FaultDefinition> activeComputeFault; ///< Recoverable fault in flight.
        Ptr<ComputeService> computeService; ///< Live busy/idle source.
    };

    /** Build a time-gated risk notice. */
    FaultDefinition MakeNotice(uint32_t nodeId,
                               const RiskEpisode& episode) const;
    /** Build a completed risk-only trace record. */
    FaultDefinition MakeRiskOnly(uint32_t nodeId,
                                 const RiskEpisode& episode,
                                 int64_t clearTimeNs) const;
    /** Build one occurred recoverable compute-fault record. */
    FaultDefinition MakeComputeFault(uint32_t nodeId,
                                     uint64_t faultId,
                                     const std::optional<RiskEpisode>& episode,
                                     double currentProbability,
                                     int64_t startTimeNs) const;
    /** Build one permanent, unannounced F3 satellite fault. */
    FaultDefinition MakeSatelliteFault(uint32_t nodeId,
                                       uint64_t faultId,
                                       int64_t startTimeNs) const;
    /** End an active compute outage at a superseding F3 timestamp. */
    void ShortenActiveComputeFault(NodeState& state,
                                   int64_t simulationTimeNs);
    /** Record one active-risk task forecast from live state before random draws. */
    void RecordProbability(uint32_t nodeId,
                           const NodeState& state,
                           int64_t simulationTimeNs);
    /** Update periodic models and/or execute F3 events in one timestamp batch. */
    void ProcessTime(int64_t simulationTimeNs, bool updateComputeModels);
    void DoDispose() override;

    bool m_configured{}; ///< Whether Configure completed.
    bool m_bound{}; ///< Whether compute services were bound.
    bool m_finalized{}; ///< Whether the trace was closed.
    bool m_probabilityAuditEnabled{}; ///< Whether live audit records are collected.
    int64_t m_simulationDurationNs{}; ///< Exclusive simulation end.
    FaultParameters m_parameters; ///< Unified model parameters.
    int64_t m_checkIntervalNs{}; ///< Converted model-check interval.
    int64_t m_recoveryDurationNs{}; ///< Converted compute outage duration.
    std::optional<F1SelfStateFaultModel> m_f1Model; ///< Active F1 pure model.
    std::optional<F2RadiationFaultModel> m_f2Model; ///< Active F2 pure model.
    std::optional<F3DebrisFaultModel> m_f3Model; ///< Active F3 schedule model.
    std::map<uint32_t, NodeState> m_nodes; ///< Node state in stable-ID order.
    std::map<int64_t, std::vector<uint32_t>> m_f3EventsByTime; ///< F3 schedule.
    uint64_t m_nextFaultId{1}; ///< Next trace identity.
    FaultTrace m_trace; ///< Completed canonical trace records.
    std::vector<ComputeFailureProbabilityRecord> m_probabilityRecords; ///< Live probabilities.
    std::vector<EventId> m_modelEvents; ///< Pre-scheduled model/F3 checks.
    Ptr<FaultController> m_faultController; ///< Sole runtime fault executor.
    Ptr<TaskCoordinator> m_taskCoordinator; ///< Bound task lifecycle owner.
    const OnlineOrbitConstellation* m_constellation{}; ///< Shared native F2 positions.
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_MODEL_ENGINE_H
