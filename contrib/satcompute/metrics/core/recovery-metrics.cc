/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/runtime/recovery-controller.h"
#include <fstream>
#include <sstream>

namespace ns3::protection
{
namespace
{
std::string
Time(int64_t ns)
{
    return ns < 0 ? "" : std::to_string(ns);
}

std::string
Ids(const std::vector<uint64_t>& values)
{
    std::string result;
    for (auto id : values)
    {
        if (!result.empty())
            result += ';';
        result += std::to_string(id);
    }
    return result;
}
} // namespace

void
RecoveryController::WriteMetrics(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
    std::ofstream file(directory / "recovery-summary.csv");
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << "task_id,attempt_generation,fault_id,fault_type,fault_time_ns,phase_at_fault,primary_"
            "node,"
            "local_node,remote_node,recovery_node,actual_work_units,local_work_units,remote_work_"
            "units,"
            "tail_bytes,committed_remote_object_id,committed_remote_bytes,valid_local_objects,"
            "pending_records,pending_local_work_units,in_flight_flows,in_flight_local_transfer_ids,"
            "in_flight_remote_transfer_ids,pending_remote_object_id,remote_merge_pending,remaining_"
            "deadline_ns,"
            "estimated_tail_ns,estimated_remote_redo_ns,"
            "chosen_path,recovery_accept_time_ns,input_start_time_ns,input_received_time_ns,input_"
            "delivery_mode,"
            "tail_start_time_ns,tail_received_time_ns,tail_commit_time_ns,recovery_compute_start_"
            "time_ns,"
            "catchup_time_ns,actual_T_catch_ns,recovery_compute_complete_time_ns,result_start_time_"
            "ns,"
            "result_complete_time_ns,result_bytes,result_delivery_mode,result_transfer_id,logical_"
            "completion,"
            "original_deadline_ns,deadline_met,reserved_idle_ns,catchup_redo_work_units,"
            "post_catchup_work_units,full_recompute_work_units,normal_protection_cost_ns,terminal_"
            "time_ns,"
            "terminal_state,terminal_reason\n";
    for (const auto& r : Summaries())
    {
        const auto& s = r.snapshot;
        std::ostringstream objects;
        for (const auto& [work, id] : s.localObjects)
        {
            if (objects.tellp() > 0)
                objects << ';';
            objects << work << ':' << id;
        }
        file << s.taskId << ",1," << r.fault.faultId << ',' << FaultTypeToString(r.fault.faultType)
             << ',' << s.faultNs << ',' << s.phase << ',' << r.primaryNode << ','
             << (s.phase == "OFF" ? "" : std::to_string(s.localNode)) << ','
             << (s.phase == "OFF" ? "" : std::to_string(s.remoteNode)) << ','
             << (r.recoveryNode ? std::to_string(*r.recoveryNode) : "") << ',' << s.actualWork
             << ',' << s.localWork << ',' << s.remoteWork << ',' << s.tailBytes << ','
             << (s.remoteObject ? std::to_string(s.remoteObject) : "") << ','
             << (s.remoteObject ? std::to_string(s.remoteBytes) : "") << ',' << objects.str() << ','
             << s.pendingRecords << ',' << Ids(s.pendingLocalWorks) << ',' << s.inFlightFlows << ','
             << Ids(s.inFlightLocalTransfers) << ',' << Ids(s.inFlightRemoteTransfers) << ','
             << (s.pendingRemoteObject ? std::to_string(s.pendingRemoteObject) : "") << ','
             << s.remoteMergePending << ',' << s.deadlineNs - s.faultNs << ','
             << Time(r.estimatedTailNs) << ',' << Time(r.estimatedRedoNs) << ',' << r.path << ','
             << Time(r.acceptedNs) << ',' << Time(r.inputStartedNs) << ','
             << Time(r.inputReceivedNs) << ',' << r.inputMode << ',' << Time(r.tailStartedNs) << ','
             << Time(r.tailReceivedNs) << ',' << Time(r.tailCommitNs) << ','
             << Time(r.computeStartedNs) << ',' << Time(r.catchupNs) << ','
             << Time(r.catchupNs < 0 ? -1 : r.catchupNs - s.faultNs) << ','
             << Time(r.computeCompleteNs) << ',' << Time(r.resultStartedNs) << ','
             << Time(r.resultCompleteNs) << ',' << r.resultBytes << ',' << r.resultMode << ','
             << (r.resultTransferId ? std::to_string(r.resultTransferId) : "") << ','
             << (r.terminalState == "COMPLETED") << ',' << s.deadlineNs << ','
             << (r.computeCompleteNs >= 0 && r.computeCompleteNs <= s.deadlineNs) << ','
             << (r.acceptedNs >= 0 ? std::to_string(r.reservedIdleNs) : "") << ','
             << (r.acceptedNs >= 0 ? std::to_string(r.catchupRedoWork) : "") << ','
             << (r.acceptedNs >= 0 ? std::to_string(r.postCatchupWork) : "") << ','
             << r.fullRecomputeWork << ',' << r.normalProtectionCostNs << ',' << Time(r.terminalNs)
             << ',' << r.terminalState << ',' << r.reason << '\n';
    }
    std::ofstream events(directory / "recovery-events.csv");
    events.exceptions(std::ios::failbit | std::ios::badbit);
    events << "task_id,attempt_generation,time_ns,event,bytes,transfer_id,delivery_mode\n";
    for (const auto& e : Events())
        events << e.taskId << ',' << e.generation << ',' << e.timeNs << ',' << e.event << ','
               << e.bytes << ',' << (e.transferId ? std::to_string(e.transferId) : "") << ','
               << e.mode << '\n';
}
} // namespace ns3::protection
