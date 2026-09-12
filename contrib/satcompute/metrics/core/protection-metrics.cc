/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-metrics.h"
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ns3
{
namespace
{
std::ofstream
Open(const std::filesystem::path& directory, const char* name)
{
    std::filesystem::create_directories(directory);
    std::ofstream file(directory / name);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    return file;
}

const char*
Kind(protection::ProtectionTransferKind kind)
{
    using K = protection::ProtectionTransferKind;
    switch (kind)
    {
    case K::INIT_BASE:
        return "INIT_BASE";
    case K::INIT_STATE:
        return "INIT_STATE";
    case K::L1:
        return "L1";
    case K::REMOTE_BATCH:
        return "REMOTE_BATCH";
    case K::RECOVERY_TAIL:
        return "RECOVERY_TAIL";
    case K::RECOVERY_INPUT:
        return "RECOVERY_INPUT";
    case K::RECOVERY_STATE:
        return "RECOVERY_STATE";
    case K::REPLICA_INPUT:
        return "REPLICA_INPUT";
    case K::REPLICA_RESULT:
        return "REPLICA_RESULT";
    case K::RECOVERY_RESULT:
        throw std::logic_error("business RESULT must not enter protection metrics");
    }
    throw std::logic_error("unknown protection flow kind");
}
} // namespace

void
WriteProtectionMetrics(const protection::CheckpointManager& manager,
                       const NetworkTransferEngine& network,
                       const std::filesystem::path& directory)
{
    auto events = Open(directory, "protection-events.csv");
    events << "task_id,attempt_generation,time_ns,event,local_node,remote_node,work_units,bytes,"
              "local_work_units,remote_work_units,actual_work_units,local_used_bytes,"
              "local_reserved_bytes,remote_used_bytes,remote_reserved_bytes,storage_node,"
              "storage_object_id,transfer_id\n";
    for (const auto& r : manager.Events())
        events << r.taskId << ',' << r.generation << ',' << r.timeNs << ',' << r.event << ','
               << r.localNode << ',' << r.remoteNode << ',' << r.work << ',' << r.bytes << ','
               << r.localWork << ',' << r.remoteWork << ',' << r.actualWork << ',' << r.localUsed
               << ',' << r.localReserved << ',' << r.remoteUsed << ',' << r.remoteReserved << ','
               << r.storageNode << ',' << r.storageObject << ',' << r.transferId << '\n';
    auto transfers = Open(directory, "protection-transfers.csv");
    transfers
        << "task_id,attempt_generation,kind,sequence,transfer_id,source_node,destination_node,"
           "source_port,destination_port,bytes,work_units,storage_object_id,requested_time_ns,"
           "start_time_ns,sender_finished_time_ns,received_bytes,received_time_ns,"
           "terminal_time_ns,state,terminal_reason,sent_bytes,capacity_waiting_time_ns\n";
    std::map<uint64_t, TransferSummaryRecord> summaries;
    for (auto row : network.CollectSummaries())
        summaries.emplace(row.transferId, std::move(row));
    for (const auto& f : manager.Flows())
    {
        if (f.key.kind == protection::ProtectionTransferKind::REPLICA_RESULT &&
            !network.IsProtectionTransfer(f.transferId))
            continue; // Winning logical RESULT is business; all replica flows have a separate ledger.
        const auto& r = summaries.at(f.transferId);
        transfers << f.key.taskId << ',' << f.key.attemptGeneration << ',' << Kind(f.key.kind)
                  << ',' << f.key.sequence << ',' << f.transferId << ',' << r.sourceSatelliteId
                  << ',' << r.destinationSatelliteId << ',' << r.sourcePort << ','
                  << r.destinationPort << ',' << f.bytes << ',' << f.work << ',' << f.storageObject
                  << ',' << f.requestedNs << ',' << r.arrivalTimeNs << ','
                  << (r.sentApplicationBytes == f.bytes ? r.lastSendTimeNs : -1) << ','
                  << r.receivedApplicationBytes << ',' << r.completionTimeNs << ','
                  << r.terminalTimeNs << ',' << r.transferState << ',' << r.terminalReason << ','
                  << r.sentApplicationBytes << ',' << r.capacityWaitingTimeNs << '\n';
    }
    auto tasks = Open(directory, "protection-task-summary.csv");
    tasks
        << "task_id,input_bytes,total_work_units,variable_state_bytes,primary_node,local_node,"
           "remote_node,delta_permille,batch_n,start_time_ns,init_complete_time_ns,stop_time_ns,"
           "cL_ns,cR_ns,local_work_units,remote_work_units,generated_count,local_commit_count,"
           "remote_commit_count,stop_reason,primary_rate_wu_per_s,init_generated_cost_count,"
           "init_committed_cost_count,local_generated_cost_count,remote_committed_cost_count,"
           "normal_protection_cost_ns,normal_protection_eq_wu,local_peak_bytes,remote_peak_bytes\n";
    tasks << std::setprecision(17);
    for (const auto& r : manager.Summaries())
        tasks << r.taskId << ',' << r.inputBytes << ',' << r.work << ',' << r.variableBytes << ','
              << r.primaryNode << ',' << r.localNode << ',' << r.remoteNode << ','
              << r.deltaPermille << ',' << r.batchN << ',' << r.startNs << ',' << r.initializationNs
              << ',' << r.stopNs << ',' << r.localCostNs << ',' << r.remoteCostNs << ','
              << r.localWork << ',' << r.remoteWork << ',' << r.generated << ',' << r.localCommits
              << ',' << r.remoteCommits << ',' << r.stopReason << ',' << r.primaryRate << ','
              << r.initGenerated << ',' << r.initCommitted << ',' << r.localGeneratedCostCount
              << ',' << r.remoteCommittedCostCount << ',' << r.normalCostNs << ','
              << static_cast<double>(r.normalCostNs) * r.primaryRate / 1e9 << ','
              << r.localPeakBytes << ',' << r.remotePeakBytes << '\n';
    auto pools = Open(directory, "protection-node-storage-summary.csv");
    pools << "node_id,capacity_bytes,used_bytes,reserved_bytes,peak_used_bytes,peak_reserved_bytes,"
             "peak_total_bytes,allocation_failures,final_used_bytes,final_reserved_bytes\n";
    for (const auto& [node, pool] : manager.Pools())
        pools << node << ',' << pool->Capacity() << ',' << pool->Used() << ',' << pool->Reserved()
              << ',' << pool->PeakUsed() << ',' << pool->PeakReserved() << ',' << pool->PeakTotal()
              << ',' << pool->AllocationFailures() << ',' << pool->Used() << ',' << pool->Reserved()
              << '\n';
    std::set<uint64_t> failedTasks;
    for (const auto& [node, pool] : manager.Pools())
        failedTasks.insert(pool->FailedTasks().begin(), pool->FailedTasks().end());
    auto final = Open(directory, "protection-finalization.json");
    if (manager.InputPolicy() == protection::InputStagingPolicy::DEFERRED)
    {
        auto staging = Open(directory, "input-staging-summary.json");
        staging << nlohmann::json({{"input_staging_policy", "deferred"},
            {"global_simultaneous_backup_peak_bytes", manager.GlobalStoragePeakBytes()}}).dump(2) << '\n';
    }
    else
        std::filesystem::remove(directory / "input-staging-summary.json");
    final << nlohmann::json({{"quiescent", manager.IsQuiescent()},
                             {"tasks_with_storage_failure", failedTasks}})
                 .dump(2)
          << '\n';
}

void
RemoveProtectionMetrics(const std::filesystem::path& directory)
{
    for (const auto name : {"protection-finalization.json",
                            "input-staging-summary.json",
                            "placement-node-summary.csv", "placement-selections.csv",
                            "placement-load-events.csv",
                            "frequency-pause-intervals.csv",
                            "f3-compute-risk-snapshots.csv",
                            "recovery-summary.csv",
                            "recovery-events.csv",
                            "protection-events.csv",
                            "protection-transfers.csv",
                            "protection-task-summary.csv",
                            "protection-node-storage-summary.csv"})
        std::filesystem::remove(directory / name);
}
} // namespace ns3
