/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/mechanism/replication/replica-manager.h"
#include <fstream>
#include <iomanip>

namespace ns3::protection
{
namespace
{
std::ofstream Open(const std::filesystem::path& directory, const char* name)
{
    std::filesystem::create_directories(directory);
    std::ofstream stream(directory / name);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << std::setprecision(17);
    return stream;
}
std::string Time(int64_t time) { return time < 0 ? "" : std::to_string(time); }
} // namespace

void ReplicaManager::WriteMetrics(const std::filesystem::path& directory) const
{
    auto tasks = Open(directory, "replica-summary.csv");
    tasks << "task_id,total_work_units,input_bytes,result_bytes,replica_requested,replica_admitted,"
        "admission_reason,request_time_ns,replica_node,original_deadline_ns,primary_faulted,replica_faulted,"
        "takeover_time_ns,winner,primary_executed_wu,replica_executed_wu,total_executed_wu,"
        "redundant_actual_wu,failed_raw_executed_wu,replica_reserved_idle_ns,replica_reserved_idle_eq_wu,"
        "planned_input_wait_ns,planned_reserved_idle_eq_wu,w_waste_actual,terminal_state,terminal_reason,terminal_time_ns\n";
    auto attempts = Open(directory, "replica-attempts.csv");
    attempts << "task_id,attempt_generation,role,node_id,stage,rate_wu_per_s,actual_work_units,actual_service_ns,"
        "reserved_time_ns,input_start_time_ns,input_received_time_ns,input_delivery_mode,input_transfer_id,"
        "compute_start_time_ns,compute_complete_time_ns,result_start_time_ns,result_complete_time_ns,"
        "result_delivery_mode,result_transfer_id,terminal_time_ns,terminal_reason,faulted,takeover_time_ns,"
        "compute_fault_immune,reserved_idle_ns\n";
    for (const auto& r : Summaries())
    {
        const auto& p = r.attempts[0];
        const auto& b = r.attempts[1];
        const auto total = p.actualWork + b.actualWork;
        const bool success = r.terminalState == "COMPLETED";
        if (success && total < r.totalWork) throw std::logic_error("successful replica work below logical W");
        const auto redundant = success ? total - r.totalWork : 0;
        const auto idle = static_cast<double>(b.reservedIdleNs) * b.rate / 1e9;
        tasks << r.taskId << ',' << r.totalWork << ',' << r.inputBytes << ',' << r.resultBytes << ','
            << r.requested << ',' << r.admitted << ',' << r.admissionReason << ',' << Time(r.requestedNs) << ','
            << (r.admitted ? std::to_string(b.node) : "") << ',' << r.deadlineNs << ',' << p.faulted << ','
            << b.faulted << ',' << Time(b.takeoverNs) << ',' << r.winner << ',' << p.actualWork << ','
            << b.actualWork << ',' << total << ',' << redundant << ',' << (success ? 0 : total) << ','
            << b.reservedIdleNs << ',' << idle << ',' << Time(b.plannedInputWaitNs) << ',';
        if (b.plannedInputWaitNs >= 0) tasks << static_cast<double>(b.plannedInputWaitNs) * b.rate / 1e9;
        tasks << ',';
        if (success) tasks << redundant + idle;
        tasks << ',' << r.terminalState << ',' << r.reason << ',' << Time(r.terminalNs) << '\n';
        for (const auto& a : r.attempts)
        {
            if (a.stage == ReplicaStage::ABSENT) continue;
            attempts << r.taskId << ',' << a.generation << ',' << (a.generation ? "replica" : "primary") << ','
                << a.node << ',' << ReplicaStageName(a.stage) << ',' << a.rate << ',' << a.actualWork << ','
                << a.actualServiceNs << ',' << Time(a.reservedNs) << ',' << Time(a.inputStartedNs) << ','
                << Time(a.inputReceivedNs) << ',' << a.inputMode << ',' << a.inputTransfer << ','
                << Time(a.computeStartedNs) << ',' << Time(a.computeCompleteNs) << ',' << Time(a.resultStartedNs) << ','
                << Time(a.resultCompleteNs) << ',' << a.resultMode << ',' << a.resultTransfer << ','
                << Time(a.terminalNs) << ',' << a.reason << ',' << a.faulted << ',' << Time(a.takeoverNs) << ','
                << a.immune << ',' << a.reservedIdleNs << '\n';
        }
    }
    auto events = Open(directory, "replica-events.csv");
    events << "task_id,attempt_generation,node_id,time_ns,event,bytes,transfer_id\n";
    for (const auto& e : m_events)
        events << e.taskId << ',' << e.generation << ',' << e.node << ',' << e.timeNs << ',' << e.event
               << ',' << e.bytes << ',' << e.transferId << '\n';
    auto transfers = Open(directory, "replica-transfers.csv");
    transfers << "task_id,attempt_generation,kind,transfer_id,source_node,destination_node,declared_bytes,"
        "sent_bytes,received_bytes,business_result,loser_result_sent_bytes,start_time_ns,terminal_time_ns,state\n";
    std::map<uint64_t, TransferSummaryRecord> network;
    for (auto row : m_network->CollectSummaries()) network.emplace(row.transferId, row);
    for (const auto& [id, flow] : m_flows)
    {
        const auto& row = network.at(id);
        const bool business = !m_network->IsProtectionTransfer(id);
        transfers << flow.taskId << ',' << flow.generation << ','
            << (flow.input ? "REPLICA_INPUT" : flow.generation ? "REPLICA_RESULT" : "PRIMARY_RESULT") << ','
            << id << ',' << row.sourceSatelliteId << ',' << row.destinationSatelliteId << ','
            << (flow.input ? m_states.at(flow.taskId)->summary.inputBytes : m_states.at(flow.taskId)->summary.resultBytes) << ','
            << row.sentApplicationBytes << ',' << row.receivedApplicationBytes << ',' << business << ','
            << (!flow.input && !business ? row.sentApplicationBytes : 0) << ',' << row.arrivalTimeNs << ','
            << row.terminalTimeNs << ',' << row.transferState << '\n';
    }
}
} // namespace ns3::protection
