/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../protection/runtime/frequency-protection-controller.h"
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace ns3::protection
{
void FrequencyProtectionController::WriteJitMetrics(const std::filesystem::path& directory) const
{
    std::ofstream decisions(directory / "jit-input-decisions.csv");
    decisions.exceptions(std::ios::badbit | std::ios::failbit);
    decisions << std::setprecision(20)
        << "task_id,time_ns,trigger,input_state,next_jit_evaluation_ns,input_transfer_s,p_on,"
           "representative_fault_time_ns,jit_should_prefetch,jit_reason,admitted,admission_reason,"
           "input_object_id,input_transfer_id\n";
    uint64_t triggers = 0, requests = 0;
    for (const auto& r : m_jitDecisions)
    {
        triggers += r.decision.shouldPrefetch; requests += r.admitted;
        decisions << r.taskId << ',' << r.timeNs << ',' << r.trigger << ',' << InputStageName(r.stage)
            << ',' << r.nextEvaluationNs << ',' << r.inputSeconds << ',' << r.decision.probabilityOn << ',';
        if (r.decision.representativeFaultTimeNs) decisions << *r.decision.representativeFaultTimeNs;
        decisions << ',' << r.decision.shouldPrefetch << ',' << r.decision.reason << ',' << r.admitted
            << ',' << r.admissionReason << ',' << r.objectId << ',' << r.transferId << '\n';
    }
    std::ofstream lifecycles(directory / "input-staging-lifecycles.csv");
    lifecycles.exceptions(std::ios::badbit | std::ios::failbit);
    lifecycles << "task_id,input_bytes,source_node,holder_node,stage,delivery_mode,input_object_id,"
        "input_transfer_id,requested_ns,registered_ns,ready_ns,fault_ns,released_ns,failed,adopted,used,"
        "prefetch_total_sent_bytes,prefetch_before_fault_sent_bytes,prefetch_after_fault_sent_bytes,"
        "prefetch_used_bytes,prefetch_unused_bytes,terminal_reason\n";
    uint64_t admitted = 0, total = 0, normal = 0, fault = 0, used = 0, failed = 0, local = 0,
        liveFlows = 0, liveObjects = 0;
    for (const auto& s : m_manager.InputStagingHistory())
    {
        const auto before = s.faultNs >= 0 ? s.sentBeforeFaultBytes : s.totalSentBytes;
        const auto after = s.totalSentBytes - before;
        const auto useful = s.used ? s.totalSentBytes : 0;
        admitted += s.registeredNs >= 0;
        failed += s.failed; local += s.local && s.registeredNs >= 0;
        total += s.totalSentBytes; normal += before; fault += after; used += useful;
        liveFlows += s.transferId && !m_tasks->GetTransferEngine()->IsTerminal(s.transferId);
        liveObjects += s.objectId && m_manager.Pools().at(s.holderNode)->Find(s.objectId);
        lifecycles << s.taskId << ',' << s.bytes << ',' << s.sourceNode << ',' << s.holderNode << ','
            << InputStageName(s.stage) << ',' << (s.local ? "LOCAL" : "NETWORK") << ','
            << s.objectId << ',' << s.transferId << ',' << s.requestedNs << ',' << s.registeredNs << ','
            << s.readyNs << ',' << s.faultNs << ',' << s.releasedNs << ',' << s.failed << ','
            << s.adopted << ',' << s.used << ',' << s.totalSentBytes << ',' << before << ',' << after << ','
            << useful << ',' << s.totalSentBytes - useful << ',' << s.terminalReason << '\n';
    }
    uint64_t readyReuse = 0, inFlightReuse = 0, faultReady = 0, faultFlight = 0, faultAbsent = 0,
        duplicates = 0, newInput = 0, storageLeaks = 0;
    for (const auto& r : m_recovery->Summaries())
    {
        readyReuse += r.inputReused && r.inputStateAtAcceptance == "READY";
        inFlightReuse += r.inputReused && r.inputStateAtAcceptance == "IN_FLIGHT";
        if (r.snapshot.input.stage == InputStage::READY) ++faultReady;
        else if (r.snapshot.input.stage == InputStage::IN_FLIGHT) ++faultFlight;
        else ++faultAbsent;
        for (const auto& f : m_manager.Flows())
            if (r.inputReused && f.key.taskId == r.snapshot.taskId &&
                f.key.kind == ProtectionTransferKind::RECOVERY_INPUT) ++duplicates;
    }
    for (const auto& f : m_manager.Flows())
        if (f.key.kind == ProtectionTransferKind::RECOVERY_INPUT)
            newInput += m_tasks->GetTransferEngine()->GetSentBytes(f.transferId);
    for (const auto& [node, pool] : m_manager.Pools()) storageLeaks += bool(pool->Used() || pool->Reserved());
    // Used means actually consumed at recovery compute start; it never means
    // merely predicted useful or successfully prefetched. Both numerator and
    // denominator cover the same complete original flow, including continuation.
    nlohmann::json audit{
        {"input_staging_policy", "jit"}, {"jit_start_benefit_enabled", m_jitStartBenefit},
        {"jit_evaluations", m_jitDecisions.size()}, {"jit_triggers", triggers},
        {"jit_requests_accepted", requests}, {"jit_admitted", admitted},
        {"ready_reuse_count", readyReuse}, {"in_flight_reuse_count", inFlightReuse},
        {"fault_ready", faultReady}, {"fault_in_flight", faultFlight}, {"fault_absent", faultAbsent},
        {"new_recovery_input_bytes", newInput}, {"duplicate_full_input_detected", duplicates},
        {"final_storage_leaks", storageLeaks},
        {"prefetch_failed_lifecycles", failed}, {"local_prefetch_count", local},
        {"prefetch_total_sent_bytes", total}, {"prefetch_before_fault_sent_bytes", normal},
        {"prefetch_after_fault_sent_bytes", fault}, {"prefetch_used_bytes", used},
        {"prefetch_unused_bytes", total-used}, {"prefetch_useful_ratio", total ? nlohmann::json(double(used)/total) : nlohmann::json(nullptr)},
        {"used_definition", "whole lifecycle actually consumed at recovery compute start"},
        {"final_live_jit_flows", liveFlows}, {"final_live_jit_objects", liveObjects},
        {"quiescent", m_manager.IsQuiescent()}, {"runtime_observation_not_independent_audit", true}};
    std::ofstream out(directory / "v7-jit-audit.json");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << audit.dump(2) << '\n';
}
} // namespace ns3::protection
