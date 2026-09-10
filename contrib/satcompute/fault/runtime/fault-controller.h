/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_CONTROLLER_H
#define SATCOMPUTE_FAULT_CONTROLLER_H

#include "fault-state.h"

#include "ns3/fault-trace.h"

#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ns3
{

class TaskCoordinator;
class SatelliteTopologyController;

class FaultControllerError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

enum class FaultEventType
{
    START,
    RECOVERY
};

const char* FaultEventTypeToString(FaultEventType eventType);

/** One time-gated event submitted by the online fault generator. */
struct GeneratedFaultEvent
{
    FaultEventType eventType{FaultEventType::START};
    FaultDefinition fault;
};

/** Immutable evidence emitted after one deterministic runtime fault event. */
struct FaultRuntimeEventRecord
{
    int64_t simulationTimeNs{};
    uint64_t faultId{};
    uint32_t nodeId{};
    FaultType faultType{FaultType::COMPUTE};
    FaultEventType eventType{FaultEventType::START};
    std::optional<int64_t> startTimeNs;
    std::optional<int64_t> durationNs;
    std::optional<double> failureProbability;
    bool satelliteAvailableAfter{true};
    bool communicationAvailableAfter{true};
    bool computeAvailableAfter{true};
    uint64_t affectedTaskCount{};
    uint64_t affectedTransferCount{};
    bool routeRecomputed{};
};

/** Schedule timestamp-batched deterministic fault start/recovery events. */
class FaultController : public Object
{
  public:
    static TypeId GetTypeId();

    FaultController();
    ~FaultController() override;

    void ConfigureGeneration(const std::vector<uint32_t>& satelliteIds,
                             int64_t simulationDurationNs);
    /** Validation-only frozen events; uses the same timestamp-batched executor. */
    void ConfigureValidationReplay(const FaultTrace& trace,
                                   const std::vector<uint32_t>& satelliteIds,
                                   int64_t simulationDurationNs);
    /**
     * Move an active generated compute recovery to the current time.
     *
     * This is used when a permanent F3 satellite fault supersedes a still
     * active recoverable compute fault. The caller must submit the F3 START in
     * the same timestamp batch immediately afterward.
     *
     * @param shortenedFault Same compute fault with duration ending now.
     * @param originalRecoveryTimeNs Previously scheduled recovery time.
     */
    void ShortenGeneratedComputeFault(const FaultDefinition& shortenedFault,
                                      int64_t originalRecoveryTimeNs);
    void SubmitGeneratedBatch(const std::vector<GeneratedFaultEvent>& events);
    void FinalizeGeneratedTrace(const FaultTrace& trace);
    void BindTopology(SatelliteTopologyController& topology);
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    const FaultTrace& GetTrace() const;
    const FaultState& GetState() const;
    const std::vector<FaultRuntimeEventRecord>& GetEvents() const;

  private:
    friend struct FaultControllerTestAccess; ///< Test-only ns event injection, never a CLI mode.
    struct ScheduledFaultEvent
    {
        FaultEventType eventType{FaultEventType::START};
        FaultDefinition fault;
    };

    void Initialize(const std::vector<uint32_t>& satelliteIds,
                    int64_t simulationDurationNs);
    void ScheduleBatch(int64_t simulationTimeNs);
    void ScheduleRecovery(const FaultDefinition& fault);
    void ProcessBatch(int64_t simulationTimeNs);
    void DoDispose() override;

    bool m_configured{};
    bool m_generatedTraceFinalized{};
    int64_t m_simulationDurationNs{};
    FaultTrace m_trace;
    FaultState m_state;
    std::set<uint32_t> m_satelliteIds;
    std::map<int64_t, std::vector<ScheduledFaultEvent>> m_batches;
    std::map<int64_t, EventId> m_batchEvents;
    std::set<int64_t> m_processedBatchTimes;
    std::set<std::pair<FaultEventType, uint64_t>> m_generatedEventKeys;
    std::vector<FaultRuntimeEventRecord> m_events;
    std::vector<EventId> m_validationEvents; ///< Frozen START submissions, validation only.
    SatelliteTopologyController* m_topology{};
    Ptr<TaskCoordinator> m_taskCoordinator;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_CONTROLLER_H
