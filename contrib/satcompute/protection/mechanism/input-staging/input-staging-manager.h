/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_INPUT_STAGING_MANAGER_H
#define SATCOMPUTE_INPUT_STAGING_MANAGER_H
#include "../../common/input-dependency.h"
#include "../../runtime/checkpoint-recovery-port.h"
#include <filesystem>

namespace ns3::protection
{
enum class OptionalInputState { ABSENT, REQUESTED, IN_FLIGHT, READY, FAILED, RELEASED };
const char* ToString(OptionalInputState state);

/** Independent object/flow lifetime. Contains no CompFRR selector or frequency dependency. */
class InputStagingManager : public InputDependencyResolver
{
  public:
    struct Record
    {
        TaskDefinition task;
        uint32_t target{};
        OptionalInputState state{OptionalInputState::ABSENT};
        uint64_t object{}, flow{}, sentAtFault{};
        int64_t requestedNs{-1}, startedNs{-1}, readyNs{-1}, faultNs{-1}, usedNs{-1}, releasedNs{-1};
        bool handedOff{}, wrongTarget{}, failed{}, closed{};
        std::string reason, refetchReason;
        EventId localEvent;
        std::function<void(bool, uint64_t)> consumer;
    };
    struct Event
    {
        uint64_t task{}, flow{}, object{}, sentBytes{};
        uint32_t target{};
        int64_t at{};
        std::string event, state, reason;
    };
    InputStagingManager(Ptr<TaskCoordinator> tasks, CheckpointRecoveryPort& manager,
                        int64_t stopNs, std::function<uint64_t(uint32_t)> free = {});
    ~InputStagingManager() override;
    /** One attempt after checkpoint admission. False/rejection never fails checkpoint protection. */
    void Request(const TaskDefinition& task, uint32_t target);
    InputDependency Resolve(const TaskDefinition& task, uint32_t target) const override;
    void Accept(uint64_t task, const InputDependency& dependency,
                std::function<void(bool, uint64_t)> completed) override;
    void Fault(uint64_t task, const TaskFaultNodeChange& change) override;
    void Freeze(uint64_t task) override;
    void ComputeStarted(uint64_t task, uint32_t target) override;
    void Release(uint64_t task) override;
    void Finalize();
    void WriteMetrics(const std::filesystem::path& directory) const;
    const std::map<uint64_t, Record>& Records() const { return m_records; }
    const std::vector<Event>& Events() const { return m_events; }
  private:
    void Started(uint64_t task);
    void Terminal(uint64_t flow, int64_t at);
    void Ready(Record& record);
    void End(Record& record, const std::string& reason, bool failed = false);
    void ReleaseObject(Record& record);
    void Log(Record& record, const std::string& event);
    void ObserveStorage();
    Ptr<TaskCoordinator> m_tasks;
    Ptr<NetworkTransferEngine> m_network;
    CheckpointRecoveryPort& m_manager;
    int64_t m_stopNs;
    std::function<uint64_t(uint32_t)> m_free;
    std::map<uint64_t, Record> m_records;
    std::map<uint64_t, uint64_t> m_flows;
    std::vector<Event> m_events;
    struct Peak { uint64_t used{}, reserved{}, total{}; };
    std::map<uint32_t, Peak> m_nodePeaks;
    Peak m_peak;
};
} // namespace ns3::protection
#endif
