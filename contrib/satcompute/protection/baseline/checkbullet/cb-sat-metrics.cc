/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-controller.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace ns3::protection::checkbullet
{
namespace
{
using J = nlohmann::json;
J Time(int64_t ns) { return ns < 0 ? J(nullptr) : J(ns); }
template <typename T> J Optional(const std::optional<T>& v) { return v ? J(*v) : J(nullptr); }
std::ofstream File(const std::filesystem::path& directory, const char* name, const char* header)
{
    std::ofstream out(directory / name);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << header << '\n';
    return out;
}
void Row(std::ostream& out, std::initializer_list<J> values)
{
    bool first = true;
    for (const auto& value : values)
    {
        if (!first) out << ',';
        first = false;
        if (value.is_null()) continue;
        if (!value.is_string()) { out << value.dump(); continue; }
        out << '"';
        for (char ch : value.get<std::string>())
        {
            if (ch == '"') out << '"';
            out << ch;
        }
        out << '"';
    }
    out << '\n';
}
std::string Logs(const std::map<uint64_t, uint64_t>& values)
{
    std::ostringstream out;
    for (const auto& [sequence, object] : values)
    {
        if (out.tellp() > 0) out << ';';
        out << sequence << ':' << object;
    }
    return out.str();
}
} // namespace

void WriteCbMetrics(const CbSatManager& manager, const CbSatRecovery& recovery,
                    const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    auto tasks = File(directory, "cb-sat-tasks.csv",
        "task_id,primary_node,backup_node,input_bytes,total_work_units,variable_state_bytes,"
        "primary_rate_wu_per_s,mtbf_seconds,reference_cost_seconds,task_seconds,interval_seconds,"
        "raw_fraction,delta_permille,target_count,interval_reason,start_time_ns,initialized_time_ns,"
        "stop_time_ns,stop_reason,generated_count,initial_commits,merge_count,local_cost_ns,remote_cost_ns,"
        "normal_protection_cost_ns,normal_protection_eq_wu,root_work_units,recoverable_work_units");
    auto records = File(directory, "cb-sat-checkpoints.csv",
        "task_id,attempt_generation,backup_node,base_version,sequence,from_work_units,to_work_units,"
        "record_bytes,captured_time_ns,scheduled_generated_time_ns,generated_time_ns,received_time_ns,"
        "log_commit_time_ns,source_object_id,destination_object_id,root_commit_time_ns");
    std::map<uint64_t, CbTaskSummary> byTask;
    struct Evidence { int64_t generated{-1}, rootCommit{-1}; uint64_t source{}, destination{}; };
    std::map<std::pair<uint64_t, uint64_t>, Evidence> evidence;
    for (const auto& event : manager.Events())
    {
        auto& e = evidence[{event.task, event.sequence}];
        if (event.event == "CAPTURE") e.source = event.object;
        if (event.event == "GENERATED") e.generated = event.timeNs;
        if (event.event == "RECEIVED") e.destination = event.object;
        if (event.event == "ROOT_COMMIT" || event.event == "MERGE_COMMIT") e.rootCommit = event.timeNs;
    }
    for (const auto& t : manager.Summaries())
    {
        byTask.emplace(t.task, t);
        const auto& h = t.interval;
        Row(tasks, {t.task, t.primary, Optional(t.backup), t.inputBytes, t.work, t.variableBytes,
            t.rate, h.mtbfSeconds, h.referenceCostSeconds, h.taskSeconds, h.intervalSeconds,
            h.rawFraction, h.deltaPermille, h.targets.size(), h.reason, t.startNs, Time(t.initializedNs),
            Time(t.stoppedNs), t.stopReason, t.generated, t.initialCommits, t.merges,
            t.localCostNs, t.remoteCostNs, t.normalCostNs,
            static_cast<double>(t.normalCostNs) * t.rate / 1e9, t.rootWork, t.recoverableWork});
        if (const auto state = manager.State(t.task))
            for (const auto& [sequence, r] : state->Records())
            {
                const auto& e = evidence[{t.task, sequence}];
                const auto& k = r.key;
                Row(records, {t.task, k.attemptGeneration, Optional(t.backup), k.baseVersion,
                    sequence, k.fromWork, k.toWork, r.bytes, r.capturedNs, r.generatedNs,
                    Time(e.generated), Time(r.receivedNs), Time(r.committedNs), e.source,
                    e.destination, Time(e.rootCommit)});
            }
    }
    auto decisions = File(directory, "cb-sat-decisions.csv",
        "task_id,decision_id,primary_node,backup_node,time_ns,work_units,mtbf_seconds,reference_cost_seconds,"
        "task_seconds,interval_seconds,raw_fraction,delta_permille,recovery_limit,storage_limit,"
        "natural_limit,implementation_limit,threshold,feasible,recovery_binds,storage_binds,cap_binds,"
        "quota_bytes,occupied_bytes,threshold_reason,reason");
    for (const auto& d : manager.Decisions())
    {
        const auto& t = byTask.at(d.task);
        const auto& h = t.interval;
        const auto& x = d.threshold;
        Row(decisions, {d.task, d.decision, t.primary, Optional(d.backup), d.timeNs, d.work,
            h.mtbfSeconds, h.referenceCostSeconds, h.taskSeconds, h.intervalSeconds, h.rawFraction,
            h.deltaPermille, Optional(x.recoveryLimit), x.storageLimit, x.naturalLimit,
            x.implementationLimit, x.value, x.feasible, x.recoveryBinds, x.storageBinds, x.capBinds,
            d.quota, d.occupied, x.reason, d.reason});
    }
    auto events = File(directory, "cb-sat-events.csv",
        "task_id,time_ns,event,node_id,object_id,sequence,work_units,bytes,transfer_id,role,"
        "root_work_units,recoverable_work_units,node_used_bytes,node_reserved_bytes");
    for (const auto& e : manager.Events())
        Row(events, {e.task, e.timeNs, e.event, e.node, e.object, e.sequence, e.work, e.bytes,
            e.transfer, e.role, e.root, e.recoverable, e.used, e.reserved});
    auto flows = File(directory, "cb-sat-transfers.csv",
        "task_id,attempt_generation,kind,sequence,transfer_id,bytes,source_node,destination_node,"
        "destination_object_id,requested_time_ns,registered_time_ns,terminal_time_ns,completed,"
        "source_port,destination_port,sent_bytes,received_bytes,sender_finished_time_ns,"
        "received_time_ns,state,terminal_reason,capacity_waiting_time_ns");
    std::map<uint64_t, TransferSummaryRecord> physical;
    for (const auto& row : manager.Network().CollectSummaries()) physical.emplace(row.transferId, row);
    for (const auto& f : manager.Flows())
    {
        const auto& p = physical.at(f.transfer);
        Row(flows, {f.task, f.generation, CbFlowName(f.kind), f.sequence, f.transfer, f.bytes,
            f.source, f.destination, f.object, f.requestedNs, Time(f.registeredNs), Time(f.terminalNs), f.completed,
            p.sourcePort, p.destinationPort, p.sentApplicationBytes, p.receivedApplicationBytes,
            Time(p.sentApplicationBytes == f.bytes ? p.lastSendTimeNs : -1), Time(p.completionTimeNs),
            p.transferState, p.terminalReason, p.capacityWaitingTimeNs});
    }
    auto storage = File(directory, "cb-sat-storage.csv",
        "node_id,capacity_bytes,peak_used_bytes,peak_reserved_bytes,peak_total_bytes,"
        "allocation_failures,final_used_bytes,final_reserved_bytes");
    for (const auto& [node, pool] : manager.Pools())
        Row(storage, {node, pool->Capacity(), pool->PeakUsed(), pool->PeakReserved(), pool->PeakTotal(),
            pool->AllocationFailures(), pool->Used(), pool->Reserved()});
    auto recoveries = File(directory, "cb-sat-recovery.csv",
        "task_id,attempt_generation,fault_id,fault_type,fault_time_ns,cutoff_time_ns,primary_node,"
        "backup_node,recovery_node,actual_work_units,root_work_units,recoverable_work_units,"
        "root_sequence,input_ready,root_ready,input_object_id,root_object_id,log_objects,"
        "resume_work_units,chosen_path,remote_busy_at_fault,checkpoint_fallback_reason,"
        "checkpoint_relocation_attempted,checkpoint_relocation_bytes,checkpoint_relocation_failure_reason,"
        "recovery_accept_time_ns,input_start_time_ns,input_received_time_ns,input_delivery_mode,"
        "state_start_time_ns,state_received_time_ns,restore_start_time_ns,state_ready_time_ns,"
        "recovery_compute_start_time_ns,catchup_time_ns,actual_T_catch_ns,recovery_compute_complete_time_ns,"
        "result_start_time_ns,result_complete_time_ns,result_bytes,result_delivery_mode,result_transfer_id,"
        "logical_completion,original_deadline_ns,deadline_met,planned_catchup_redo_wu,planned_post_catchup_wu,"
        "actual_catchup_redo_wu,actual_post_catchup_wu,actual_total_recovery_wu,actual_recovery_service_ns,"
        "primary_rate_wu_per_s,recovery_rate_wu_per_s,reserved_idle_ns,recovery_reserved_idle_eq_wu,"
        "restore_processing_ns_included_in_idle,tail_request_count,tail_bytes,terminal_time_ns,terminal_state,terminal_reason");
    for (const auto& r : recovery.Summaries())
    {
        const auto& s = r.snapshot;
        const auto& state = s.state;
        Row(recoveries, {r.task, 1, r.fault.faultId, FaultTypeToString(r.fault.faultType),
            Optional(r.fault.startTimeNs), s.cutoffNs, r.primary,
            state.rootReady || state.inputReady ? J(state.backupNode) : J(nullptr), Optional(r.node),
            s.actualWork, state.rootWork, state.recoverableWork, state.rootSequence,
            state.inputReady, state.rootReady, s.inputObject, s.rootObject, Logs(s.logObjects),
            r.resumeWork, r.path, r.remoteBusy, r.fallbackReason, r.relocationAttempted,
            r.relocationBytes, r.relocationFailure, Time(r.acceptedNs), Time(r.inputStartedNs),
            Time(r.inputReadyNs), r.inputMode, Time(r.stateStartedNs), Time(r.stateReceivedNs),
            Time(r.restoreStartedNs), Time(r.stateReadyNs), Time(r.computeStartedNs), Time(r.catchupNs),
            Time(r.catchupNs < 0 ? -1 : r.catchupNs - s.cutoffNs), Time(r.computedNs),
            Time(r.resultStartedNs), Time(r.resultNs), r.resultBytes, r.resultMode, r.resultTransfer,
            r.terminal == "COMPLETED", s.deadlineNs, r.computedNs >= 0 && r.computedNs <= s.deadlineNs,
            r.plannedCatchupWu, r.plannedRemainingWu, r.actualCatchupWu, r.actualRemainingWu,
            r.actualRecoveryWu, r.actualServiceNs, r.primaryRate, r.recoveryRate, r.reservedIdleNs,
            static_cast<double>(r.reservedIdleNs) * r.recoveryRate / 1e9, r.restoreProcessingNs,
            0, 0, Time(r.terminalNs), r.terminal, r.reason});
    }
}
} // namespace ns3::protection::checkbullet
