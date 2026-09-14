/* SPDX-License-Identifier: GPL-2.0-only */
#include "checkpoint-recovery-port.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <stdexcept>
namespace ns3::protection
{
namespace
{
int64_t Now() { return Simulator::Now().GetNanoSeconds(); }
void Require(bool value, const char* message)
{
    if (!value) throw std::logic_error(message);
}
}
RecoverySnapshot FreezePrimaryRecoverySnapshot(Ptr<TaskCoordinator> tasks, uint64_t id, int64_t at)
{
    RecoverySnapshot result;
    result.taskId = id;
    result.faultNs = at;
    const auto task = std::find_if(tasks->GetTaskRuntimes().begin(),
                                   tasks->GetTaskRuntimes().end(),
                                   [id](const auto& t) { return t.definition.taskId == id; });
    Require(task != tasks->GetTaskRuntimes().end() && task->state == TASK_RUNNING && at == Now(),
            "snapshot requires the live primary at fault time");
    result.deadlineNs = task->computeDeadlineTimeNs;
    for (auto service : tasks->GetComputeServices())
        if (service->GetNodeId() == task->definition.computeNodeId)
            result.actualWork = static_cast<uint64_t>(std::min<unsigned __int128>(
                task->definition.computeWorkUnits,
                static_cast<unsigned __int128>(at - task->computeStartTimeNs) *
                    service->GetComputeRateWorkUnitsPerSecond() / 1000000000));
    return result;
}
ProtectionEvent MakeRecoveryEvidence(const RecoverySnapshot& s, const std::string& event,
                                     uint64_t bytes, uint64_t transferId, int64_t at,
                                     const BackupPools& pools)
{
    ProtectionEvent row;
    row.taskId = s.taskId;
    row.generation = 1;
    row.timeNs = at;
    row.event = event;
    row.localNode = s.localNode;
    row.remoteNode = s.remoteNode;
    row.bytes = bytes;
    row.localWork = s.localWork;
    row.remoteWork = s.remoteWork;
    row.actualWork = s.actualWork;
    row.transferId = transferId;
    if (s.phase != "OFF")
    {
        row.localUsed = pools.at(s.localNode)->Used();
        row.localReserved = pools.at(s.localNode)->Reserved();
        row.remoteUsed = pools.at(s.remoteNode)->Used();
        row.remoteReserved = pools.at(s.remoteNode)->Reserved();
    }
    return row;
}
void CheckpointRecoveryPort::QueueRecovery(ProtectionTransferKey key, uint32_t source, uint32_t destination,
                                           uint64_t bytes, uint64_t work, uint64_t object,
                                           std::function<bool()> live,
                                           std::function<void(uint64_t)> registered)
{
    Require(key.attemptGeneration == 1 && source != destination && bytes && live && registered,
            "recovery network request requires real cross-node bytes and attempt guards");
    const auto time = Now();
    Transfers().Queue(time,
        ProtectionTransferRequest{key, source, destination, bytes, work, object, time,
                std::move(live), std::move(registered)},
        "duplicate recovery request");
}
} // namespace ns3::protection
