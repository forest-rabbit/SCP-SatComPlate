/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/policy/compfrr/compfrr-controller.h"
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace ns3::protection
{
using Json = nlohmann::json;
void CompFrrController::WriteInputAdmissionAudit(const std::filesystem::path& directory) const
{
    if (!m_optionalInput) return;
    std::ofstream out(directory / "input-admission-decisions.csv");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(21)
        << "task_id,profile,policy,start_time_ns,trigger,source,local,remote,input_bytes,P_F,first_sample_ns,finish_exclusive,T_ser_ns,T_net_ns,G_ser_ns,G_net_ns,cost_ns,decision,reason\n";
    for (const auto& [a, d] : m_inputAdmissions)
        // Preserve the algorithm identifier and CSV schema; public inputPolicy is selective.
        out << a.task.taskId << ',' << TaskProfileToString(a.task.taskProfile) << ",ser-break-even,"
            << a.timeNs << ',' << a.trigger << ',' << a.task.sourceNodeId << ',' << a.pair.localNode << ','
            << a.pair.remoteNode << ',' << a.task.inputBytes << ','
            << (a.prediction ? Json(a.prediction->predictedFailureProbability).dump() : "") << ','
            << a.firstSampleNs << ',' << a.finishExclusive << ',' << d.serializationNs << ',' << d.networkReadyNs << ','
            << d.serialGainNs << ',' << d.networkGainNs << ',' << d.costNs << ',' << (d.send ? "SEND" : "DEFER")
            << ',' << d.reason << '\n';
    std::ofstream admission(directory / "compfrr-policy-aware-admission.csv");
    admission.exceptions(std::ios::badbit | std::ios::failbit);
    admission << std::setprecision(21)
              << "task_id,time_ns,decision_trigger,stage,local,remote,candidate_index,"
                 "selective_dryrun_decision,selective_reason,selective_pf,selective_u_ser_pot,"
                 "selective_t_ser_s,legacy_fault_input_s,policy_aware_input_admission_s,"
                 "legacy_frequency_reason,policy_aware_frequency_reason,legacy_deadline_feasible,"
                 "policy_aware_deadline_feasible,rescued_by_policy_aware_input,anchor_remote,"
                 "final_remote,is_anchor,is_final_pair,final_pair_revalidated,fault_hit_same_batch,"
                 "start_committed,runtime_prefetch_admission_success\n";
    for (const auto& row : m_decisions)
        for (const auto& record : row.policyAwareInputAudits)
        {
            admission << record.taskId << ',' << record.timeNs << ',' << record.trigger << ','
                      << record.stage << ',' << record.local << ',' << record.remote << ','
                      << record.candidateIndex << ',' << (record.selective.send ? "SEND" : "DEFER")
                      << ',' << record.selective.reason << ',' << record.predictedFailureProbability
                      << ',';
            if (record.selective.serializationNs > 0)
                admission << record.selective.serialGainNs / record.selective.serializationNs;
            admission << ',' << record.selective.serializationNs / 1e9
                      << ',' << record.legacyFaultInputSeconds << ',' << record.admissionSeconds << ','
                      << record.legacyFrequencyReason << ',' << record.policyAwareFrequencyReason << ','
                      << record.legacyDeadlineFeasible << ',' << record.policyAwareDeadlineFeasible
                      << ',' << record.rescuedByPolicyAwareInput << ',';
            if (row.candidateCoverage && row.candidateCoverage->anchorRemote)
                admission << *row.candidateCoverage->anchorRemote;
            admission << ',';
            if (row.candidateCoverage && row.candidateCoverage->finalRemote)
                admission << *row.candidateCoverage->finalRemote;
            admission << ',' << record.anchor << ',' << record.finalPair << ','
                      << record.finalPairRevalidated << ',' << record.faultHitSameBatch << ','
                      << record.startCommitted << ',';
            if (record.runtimePrefetchAdmissionSuccess)
                admission << *record.runtimePrefetchAdmissionSuccess;
            admission << '\n';
        }
    m_optionalInput->WriteMetrics(directory);
}

void InputStagingManager::WriteMetrics(const std::filesystem::path& directory) const
{
    std::ofstream events(directory / "input-prefetch-events.csv");
    events.exceptions(std::ios::badbit | std::ios::failbit);
    events << "task_id,time_ns,event,state,target,object_id,flow_id,sent_bytes_so_far,reason\n";
    for (const auto& e : m_events)
        events << e.task << ',' << e.at << ',' << e.event << ',' << e.state << ',' << e.target << ','
            << e.object << ',' << e.flow << ',' << e.sentBytes << ',' << e.reason << '\n';
    Json tasks = Json::array(), nodes = Json::array();
    uint64_t total = 0, used = 0, noFault = 0, wrong = 0, failed = 0, other = 0;
    uint64_t normal = 0, recovery = 0, started = 0, ready = 0, local = 0;
    for (const auto& [id, r] : m_records)
    {
        const auto sent = r.flow ? m_network->GetSentBytes(r.flow) : 0;
        const auto normalBytes = r.faultNs < 0 ? sent : r.sentAtFault;
        total += sent; normal += normalBytes; recovery += sent - normalBytes;
        started += r.startedNs >= 0 && r.flow; ready += r.readyNs >= 0; local += r.startedNs >= 0 && !r.flow;
        // Mutually exclusive complete-lifecycle byte categories. Separate flags preserve
        // overlapping causes without counting one physical byte more than once.
        const auto category = r.usedNs >= 0 ? "USED" : r.wrongTarget ? "WRONG_TARGET" :
            r.failed ? "FAILED_OR_CANCELLED" : r.faultNs < 0 ? "NO_FAULT" : "NOT_CONSUMED";
        if (r.usedNs >= 0) used += sent;
        else if (r.wrongTarget) wrong += sent;
        else if (r.failed) failed += sent;
        else if (r.faultNs < 0) noFault += sent;
        else other += sent;
        tasks.push_back({{"task_id", id}, {"source", r.task.sourceNodeId}, {"target", r.target},
            {"input_bytes", r.task.inputBytes}, {"flow_id", r.flow}, {"object_id", r.object},
            {"state", ToString(r.state)}, {"requested_ns", r.requestedNs}, {"started_ns", r.startedNs},
            {"ready_ns", r.readyNs}, {"fault_ns", r.faultNs}, {"used_ns", r.usedNs}, {"released_ns", r.releasedNs},
            {"sent_bytes", sent}, {"received_bytes", r.flow ? m_network->GetReceivedBytes(r.flow) : uint64_t{0}},
            {"normal_sent_bytes", normalBytes}, {"post_fault_sent_bytes", sent-normalBytes},
            {"used_bytes", r.usedNs >= 0 ? sent : uint64_t{0}}, {"unused_bytes", r.usedNs < 0 ? sent : uint64_t{0}},
            {"logical_local_bytes", r.readyNs >= 0 && !r.flow ? r.task.inputBytes : uint64_t{0}},
            {"handed_off", r.handedOff}, {"wrong_target", r.wrongTarget}, {"failed", r.failed},
            {"byte_category", category}, {"reason", r.reason}, {"refetch_reason", r.refetchReason}});
    }
    for (const auto& [node, peak] : m_nodePeaks)
        nodes.push_back({{"node", node}, {"peak_used_bytes", peak.used},
            {"peak_reserved_bytes", peak.reserved}, {"peak_total_bytes", peak.total}});
    uint64_t liveUsed = 0, liveReserved = 0;
    for (const auto& [id, r] : m_records)
        if (const auto object = r.object ? m_manager.Pool(r.target).Find(r.object) : nullptr)
            (object->reserved ? liveReserved : liveUsed) += object->bytes;
    std::ofstream summary(directory / "input-prefetch-summary.json");
    summary.exceptions(std::ios::badbit | std::ios::failbit);
    summary << Json{{"proactive_lifetime_accounting", true}, {"PREFETCH_USED_at", "ACTUAL_RECOVERY_COMPUTE_STARTED"},
        {"request_count", m_records.size()}, {"network_started_count", started}, {"local_started_count", local},
        {"ready_count", ready}, {"B_prefetch_total", total}, {"B_prefetch_used", used},
        {"B_prefetch_unused", total-used}, {"B_prefetch_no_fault", noFault},
        {"B_prefetch_wrong_target", wrong}, {"B_prefetch_failed_or_cancelled", failed},
        {"B_prefetch_not_consumed", other}, {"normal_sent_bytes", normal}, {"post_fault_sent_bytes", recovery},
        {"peak_used_bytes", m_peak.used}, {"peak_reserved_bytes", m_peak.reserved}, {"peak_total_bytes", m_peak.total},
        {"used_bytes_at_end", liveUsed}, {"reserved_bytes_at_end", liveReserved},
        {"node_peaks", nodes}, {"tasks", tasks}}.dump(2) << '\n';
}
} // namespace ns3::protection
