/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_CONTROLLER_H
#define SATCOMPUTE_FAULT_CONTROLLER_H

#include "fault-state.h"
#include "fault-trace.h"

#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ns3
{

class TaskCoordinator;

class FaultControllerError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

enum class FaultEventType
{
    NOTICE,
    START,
    RECOVERY
};

const char* FaultEventTypeToString(FaultEventType eventType);

/** Immutable evidence emitted after one deterministic runtime fault event. */
struct FaultRuntimeEventRecord
{
    int64_t simulationTimeNs{};
    uint64_t faultId{};
    uint32_t nodeId{};
    FaultType faultType{FaultType::COMPUTE};
    FaultEventType eventType{FaultEventType::NOTICE};
    std::optional<int64_t> noticeTimeNs;
    int64_t startTimeNs{};
    std::optional<int64_t> durationNs;
    std::optional<double> failureProbability;
    bool satelliteAvailableAfter{true};
    bool communicationAvailableAfter{true};
    bool computeAvailableAfter{true};
    uint64_t affectedTaskCount{};
    uint64_t affectedTransferCount{};
    bool routeRecomputed{};
};

/** Schedule timestamp-batched deterministic fault notice/start/recovery events. */
class FaultController : public Object
{
  public:
    static TypeId GetTypeId();

    FaultController();
    ~FaultController() override;

    void Configure(const FaultTrace& trace,
                   const std::vector<uint32_t>& satelliteIds,
                   int64_t simulationDurationNs);
    void BindTaskCoordinator(Ptr<TaskCoordinator> taskCoordinator);

    const FaultTrace& GetTrace() const;
    const FaultState& GetState() const;
    const std::vector<FaultRuntimeEventRecord>& GetEvents() const;

  private:
    struct ScheduledFaultEvent
    {
        FaultEventType eventType{FaultEventType::NOTICE};
        FaultDefinition fault;
    };

    void ProcessBatch(int64_t simulationTimeNs);
    void DoDispose() override;

    bool m_configured{};
    int64_t m_simulationDurationNs{};
    FaultTrace m_trace;
    FaultState m_state;
    std::map<int64_t, std::vector<ScheduledFaultEvent>> m_batches;
    std::vector<EventId> m_batchEvents;
    std::vector<FaultRuntimeEventRecord> m_events;
    Ptr<TaskCoordinator> m_taskCoordinator;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_CONTROLLER_H
