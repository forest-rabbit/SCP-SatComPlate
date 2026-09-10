/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-shadow-evaluator.h"
#include "compfrr-shadow-recorder.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace ns3::compfrr
{
namespace
{
using Json = nlohmann::json;
constexpr int64_t SECOND = 1000000000;

int64_t
Now()
{
    return Simulator::Now().GetNanoSeconds();
}

int64_t
Duration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0 || seconds >= 9e9)
        throw std::invalid_argument("invalid shadow duration");
    // A virtual operation must never become effective before its analytical duration.
    return static_cast<int64_t>(std::ceil(static_cast<long double>(seconds) * SECOND));
}

Json
Optional(const std::optional<double>& value)
{
    return value ? Json(*value) : Json();
}
} // namespace

ShadowEvaluator::ShadowEvaluator(Ptr<TaskCoordinator> tasks,
                                 Ptr<FaultModelEngine> risk,
                                 const ComputeProfile& profile,
                                 uint64_t bandwidthBps,
                                 const std::filesystem::path& output)
    : m_tasks(tasks), m_risk(risk), m_bandwidth(bandwidthBps / 8.0), m_output(output)
{
    if (!tasks || !risk || bandwidthBps != 10000000000ULL)
        throw std::invalid_argument(
            "G4 shadow requires tasks, online generate risk, and actual 10 Gbps ISLs");
    for (const auto& task : tasks->GetTaskRuntimes())
    {
        const auto id = task.definition.taskId;
        m_real.emplace(id, &task);
        ShadowTaskState state;
        state.layout = MakeWorkloadLayout(task.definition);
        state.costs = CostTier(state.layout.variableBytes);
        state.rate = GetComputeNodeProfile(profile, task.definition.computeNodeId)
                         .computeRateWorkUnitsPerSecond;
        state.summary = {{"task_id", id},
                         {"task_profile", TaskProfileToString(task.definition.taskProfile)},
                         {"input_bytes", task.definition.inputBytes},
                         {"W", state.layout.work},
                         {"K_variable", state.layout.variableBytes},
                         {"compute_node_id", task.definition.computeNodeId},
                         {"compute_rate_wu_per_s", state.rate},
                         {"cost_tier", state.costs.tier},
                         {"cL_s", state.costs.local},
                         {"cR_s", state.costs.remote},
                         {"ever_start", false},
                         {"init_complete", false},
                         {"initialization_fault_miss", false},
                         {"initialization_completion_abort", false}};
        m_states.emplace(id, std::move(state));
    }
    tasks->ConnectTaskObserver(MakeCallback(&ShadowEvaluator::OnTask, this));
}

ShadowEvaluator::~ShadowEvaluator()
{
    m_tasks->DisconnectTaskObserver(MakeCallback(&ShadowEvaluator::OnTask, this));
    for (auto& [id, s] : m_states)
    {
        if (s.nextTarget.IsPending())
            Simulator::Cancel(s.nextTarget);
        for (auto& event : s.events)
            if (event.IsPending())
                Simulator::Cancel(event);
    }
}

bool
ShadowEvaluator::Running(uint64_t id) const
{
    const auto& task = *m_real.at(id);
    return task.state == TASK_RUNNING &&
           m_tasks->IsComputeAvailable(task.definition.computeNodeId) &&
           m_tasks->IsSatelliteAvailable(task.definition.computeNodeId);
}

uint64_t
ShadowEvaluator::CompletedWork(uint64_t id) const
{
    const auto& task = *m_real.at(id);
    if (task.computeStartTimeNs < 0)
        return 0;
    const auto& s = m_states.at(id);
    const auto work = static_cast<unsigned __int128>(s.rate) *
                      static_cast<uint64_t>(Now() - task.computeStartTimeNs) / SECOND;
    return static_cast<uint64_t>(std::min(work, static_cast<unsigned __int128>(s.layout.work)));
}

DecisionInput
ShadowEvaluator::Input(uint64_t id) const
{
    const auto& s = m_states.at(id);
    const auto& task = *m_real.at(id);
    DecisionInput in;
    in.inputBytes = task.definition.inputBytes;
    in.work = s.layout.work;
    in.variableBytes = s.layout.variableBytes;
    in.rate = s.rate;
    in.bandwidth = m_bandwidth;
    in.progress = static_cast<double>(CompletedWork(id)) / s.layout.work;
    in.remainingSeconds =
        std::max<int64_t>(0, task.computeStartTimeNs + task.baselineComputeTimeNs - Now()) /
        static_cast<double>(SECOND);
    in.deadlineSlack = (task.computeDeadlineTimeNs - Now()) / static_cast<double>(SECOND) -
                       (s.layout.work - CompletedWork(id)) / static_cast<double>(s.rate);
    in.costs = s.costs;
    // Candidate resources are expressly nonbinding in G4, not real reservations.
    return in;
}

void
ShadowEvaluator::Event(uint64_t id, const std::string& type, Json detail)
{
    if (detail.is_null())
        detail = Json::object();
    detail["time_ns"] = Now();
    detail["task_id"] = id;
    detail["event"] = type;
    m_events.push_back(std::move(detail));
}

void
ShadowEvaluator::OnTask(const TaskEventRecord& event)
{
    auto& s = m_states.at(event.taskId);
    if (event.toState == TASK_RUNNING)
    {
        // Let the real dispatch and all current-time state updates finish first.
        s.events.push_back(Simulator::ScheduleNow(&ShadowEvaluator::Decide, this, event.taskId));
    }
    else if (event.fromState == TASK_RUNNING)
    {
        Stop(event.taskId, event.toState == TASK_FAILED);
    }
}

void
ShadowEvaluator::Decide(uint64_t id)
{
    if (!Running(id))
        return;
    auto& s = m_states.at(id);
    auto in = Input(id);
    if (in.remainingSeconds <= 0)
        return;
    const auto& task = *m_real.at(id);
    const auto one = m_risk->QueryComputeRisk(task.definition.computeNodeId, SECOND);
    const auto finish =
        m_risk->QueryComputeRisk(task.definition.computeNodeId,
                                 task.computeStartTimeNs + task.baselineComputeTimeNs - Now());
    Json row = {{"time_ns", Now()},
                {"task_id", id},
                {"task_profile", TaskProfileToString(task.definition.taskProfile)},
                {"mode_before", s.mode},
                {"progress", in.progress},
                {"remaining_compute_s", in.remainingSeconds},
                {"q_comp_1s", Optional(one.pCompute)},
                {"p_finish", Optional(finish.pCompute)},
                {"k_variable_bytes", s.layout.variableBytes},
                {"cost_tier", s.costs.tier},
                {"cL_s", s.costs.local},
                {"cR_s", s.costs.remote},
                {"r_max_s", in.deadlineSlack},
                {"start_triggered", false},
                {"query_available", one.pCompute && finish.pCompute}};
    if (one.pCompute && finish.pCompute && (s.mode == "OFF" || s.mode == "ON"))
    {
        in.qOneSecond = *one.pCompute;
        in.pFinish = *finish.pCompute;
        const bool on = s.mode == "ON";
        const auto choice = SelectFrequency(in, on);
        row["feasible_candidate_count"] = choice.feasibleCount;
        row["j_off_s"] = in.pFinish * RecomputeCatchUp(in);
        if (choice.best)
        {
            const auto& best = *choice.best;
            row["best_delta"] = best.deltaPermille / 1000.0;
            row["best_n"] = best.remoteEvery;
            row["best_remote_interval"] = best.remoteEvery * best.deltaPermille / 1000.0;
            row[on ? "j_on_s" : "j_start_s"] =
                best.objective + (on ? 0 : s.costs.local + s.costs.remote);
        }
        if (!on)
        {
            const uint64_t initial = s.layout.Floor(CompletedWork(id));
            const double init = InitializationTime(in, s.layout.StateAt(initial));
            row["t_init_s"] = init;
            if (ShouldStartProtection(in, choice, init))
            {
                s.BeginInitialization(Now(),
                                      initial,
                                      choice.best->deltaPermille,
                                      choice.best->remoteEvery);
                s.summary.update({{"ever_start", true},
                                  {"start_time_ns", Now()},
                                  {"start_progress", in.progress},
                                  {"start_p_finish", in.pFinish},
                                  {"start_q_1s", in.qOneSecond},
                                  {"initial_legal_work", initial},
                                  {"initial_state_bytes", s.layout.StateAt(initial)},
                                  {"init_planned_complete_time_ns", Now() + Duration(init)}});
                row["start_triggered"] = true;
                Event(id, "START", {{"initial_legal_work", initial}, {"t_init_s", init}});
                s.events.push_back(Simulator::Schedule(NanoSeconds(Duration(init)),
                                                       &ShadowEvaluator::Initialized,
                                                       this,
                                                       id));
            }
        }
        else if (choice.best)
        {
            const auto& best = *choice.best;
            const bool deltaChanged = s.delta != best.deltaPermille;
            const bool nChanged = s.n != best.remoteEvery;
            s.deltaChanges += deltaChanged;
            s.nChanges += nChanged;
            s.configChanges += deltaChanged || nChanged;
            s.delta = best.deltaPermille;
            s.n = best.remoteEvery;
            if (deltaChanged || nChanged)
                Event(id,
                      "RECONFIGURE",
                      {{"delta_permille", s.delta},
                       {"n", s.n},
                       {"pending_l1_count", s.pending.size()}});
            FormBatches(id);
            if (deltaChanged || !s.nextTarget.IsPending())
                PlanTarget(id);
        }
        else
        {
            // ON never resets. Pause new targets; existing operations retain semantics.
            if (s.nextTarget.IsPending())
                Simulator::Cancel(s.nextTarget);
            Event(id, "NO_FEASIBLE_CANDIDATE");
        }
    }
    row["mode_after"] = s.mode;
    m_decisions.push_back(std::move(row));
    if (in.remainingSeconds > 1.0)
        s.events.push_back(Simulator::Schedule(Seconds(1), &ShadowEvaluator::Decide, this, id));
}

void
ShadowEvaluator::Initialized(uint64_t id)
{
    auto& s = m_states.at(id);
    if (!Running(id) || s.mode != "INITIALIZING")
        return;
    if (!s.CompleteInitialization(Now()))
        return;
    s.summary.update({{"init_complete", true}, {"init_complete_time_ns", Now()}});
    Event(id, "ON", {{"local_work", s.localWork}, {"remote_work", s.remoteWork}});
    PlanTarget(id);
}

void
ShadowEvaluator::PlanTarget(uint64_t id)
{
    auto& s = m_states.at(id);
    if (s.nextTarget.IsPending())
        Simulator::Cancel(s.nextTarget);
    if (!Running(id) || s.mode != "ON")
        return;
    // Reconfiguration acts only on future work. No retrospective checkpoint emission.
    const uint64_t base = std::max(s.triggeredWork, CompletedWork(id));
    const auto target = s.layout.NextProgress(base, s.delta, s.triggeredWork);
    if (!target)
        return;
    const auto& task = *m_real.at(id);
    const auto product = static_cast<unsigned __int128>(*target) * SECOND;
    const int64_t at =
        task.computeStartTimeNs + static_cast<int64_t>((product + s.rate - 1) / s.rate);
    s.nextTarget = Simulator::Schedule(NanoSeconds(std::max<int64_t>(0, at - Now())),
                                       &ShadowEvaluator::TriggerLocal,
                                       this,
                                       id,
                                       *target);
}

void
ShadowEvaluator::TriggerLocal(uint64_t id, uint64_t work)
{
    auto& s = m_states.at(id);
    if (!Running(id) || s.mode != "ON" || work <= s.triggeredWork)
        return;
    s.triggeredWork = work;
    Event(id, "L1_TRIGGER", {{"target_work", work}});
    s.events.push_back(Simulator::Schedule(NanoSeconds(Duration(s.costs.local)),
                                           &ShadowEvaluator::CompleteLocal,
                                           this,
                                           id,
                                           work,
                                           Now()));
    PlanTarget(id);
}

void
ShadowEvaluator::CompleteLocal(uint64_t id, uint64_t work, int64_t triggeredNs)
{
    auto& s = m_states.at(id);
    if (!Running(id) || s.mode != "ON")
        return;
    if (work <= s.localWork)
        throw std::logic_error("duplicate/out-of-order virtual L1 completion");
    const uint64_t bytes = s.layout.StateAt(work) - s.layout.StateAt(s.localWork);
    Event(id,
          "L1_DONE",
          {{"trigger_time_ns", triggeredNs},
           {"local_work", work},
           {"previous_local_work", s.localWork},
           {"delta_state_bytes_actual", bytes},
           {"delta_progress_actual", (work - s.localWork) / static_cast<double>(s.layout.work)}});
    s.localWork = work;
    s.peakLocalStateBytes = std::max(s.peakLocalStateBytes, s.layout.StateAt(work));
    ++s.localCount;
    s.pending.emplace_back(work, bytes);
    s.peakPendingLocalCount = std::max<uint64_t>(s.peakPendingLocalCount, s.pending.size());
    s.peakLocalTailBytes = std::max(s.peakLocalTailBytes,
                                    s.layout.StateAt(s.localWork) - s.layout.StateAt(s.remoteWork));
    FormBatches(id);
}

void
ShadowEvaluator::FormBatches(uint64_t id)
{
    auto& s = m_states.at(id);
    while (s.n > 0 && s.pending.size() >= static_cast<size_t>(s.n))
    {
        uint64_t bytes = 0, work = 0;
        for (int i = 0; i < s.n; ++i)
        {
            work = s.pending.front().first;
            bytes += s.pending.front().second;
            s.pending.pop_front();
        }
        const int64_t start = std::max(Now(), s.remoteDoneNs);
        s.remoteDoneNs = start + Duration(bytes / m_bandwidth + s.costs.remote);
        ++s.remoteInFlight;
        const uint64_t batch = ++s.batchSequence;
        s.peakRemoteInFlight = std::max(s.peakRemoteInFlight, s.remoteInFlight);
        Event(id,
              "REMOTE_BATCH",
              {{"batch_id", batch},
               {"batch_bytes", bytes},
               {"batch_end_work", work},
               {"n_at_creation", s.n},
               {"start_time_ns", start},
               {"done_time_ns", s.remoteDoneNs}});
        s.events.push_back(Simulator::Schedule(NanoSeconds(s.remoteDoneNs - Now()),
                                               &ShadowEvaluator::CompleteRemote,
                                               this,
                                               id,
                                               work,
                                               bytes,
                                               batch));
    }
}

void
ShadowEvaluator::CompleteRemote(uint64_t id, uint64_t work, uint64_t bytes, uint64_t batch)
{
    auto& s = m_states.at(id);
    if (!Running(id) || s.mode != "ON")
        return;
    if (work <= s.remoteWork || work > s.localWork)
        throw std::logic_error("invalid virtual remote state advancement");
    s.remoteWork = work;
    s.peakRemoteStateBytes = std::max(s.peakRemoteStateBytes, s.layout.StateAt(work));
    ++s.remoteCount;
    --s.remoteInFlight;
    Event(id, "REMOTE_DONE", {{"batch_id", batch}, {"batch_bytes", bytes}, {"remote_work", work}});
}

void
ShadowEvaluator::Stop(uint64_t id, bool failed)
{
    auto& s = m_states.at(id);
    if (s.stopNs >= 0)
        return;
    const std::string before = s.mode;
    const FaultTaskImpactRecord* observed = nullptr;
    if (failed)
        for (const auto& impact : m_tasks->GetFaultTaskImpacts())
            if (impact.taskId == id && impact.impactTimeNs == Now() && impact.progressValid)
                observed = &impact;
    if (observed)
    {
        auto in = Input(id);
        in.progress = observed->completedWorkUnits / static_cast<double>(s.layout.work);
        const double l = s.localWork / static_cast<double>(s.layout.work);
        const double r = s.remoteWork / static_cast<double>(s.layout.work);
        const bool on = before == "ON";
        const auto shadow = EstimateCatchUp(in, on, l, r);
        const auto off = EstimateCatchUp(in, false, 0, 0);
        const auto& fault = observed->fault;
        const bool primary = fault.faultType == FaultType::COMPUTE;
        const std::string source = !primary                               ? "F3"
                                   : fault.f1Occurred && fault.f2Occurred ? "F1+F2"
                                   : fault.f1Occurred                     ? "F1"
                                                                          : "F2";
        const std::string classification = on                         ? "PROTECTED_ON"
                                           : before == "INITIALIZING" ? "RECOMPUTE_INIT_MISS"
                                                                      : "RECOMPUTE_OFF";
        const double normal = s.NormalCost();
        const double allOffWaste = off.executedWasteWu + off.idleWasteWu;
        const double recoveryWaste = shadow.executedWasteWu + shadow.idleWasteWu;
        Json row = {{"fault_id", fault.faultId},
                    {"fault_type", source},
                    {"task_id", id},
                    {"task_profile", TaskProfileToString(m_real.at(id)->definition.taskProfile)},
                    {"fault_time", Now()},
                    {"primary_f1_f2", primary},
                    {"mode_at_fault", before},
                    {"x_f", in.progress},
                    {"l_f", l},
                    {"r_f", r},
                    {"protected", on},
                    {"initialization_miss", before == "INITIALIZING"},
                    {"recompute", !on},
                    {"fault_classification", classification},
                    {"input_bytes", m_real.at(id)->definition.inputBytes},
                    {"work_units", s.layout.work},
                    {"fault_q_comp", Optional(fault.failureProbability)},
                    {"T_catch_shadow", shadow.seconds},
                    {"T_catch_all_off", off.seconds},
                    {"normal_cost_before_fault", normal},
                    {"normal_waste_wu", normal * s.rate},
                    {"W_executed_extra", normal * s.rate + shadow.executedWasteWu},
                    {"W_idle_reserved", shadow.idleWasteWu},
                    {"W_waste_total", normal * s.rate + recoveryWaste},
                    {"recompute_input_wait_s", in.inputBytes / in.bandwidth},
                    {"recompute_redo_s", in.work * in.progress / in.rate},
                    {"recompute_idle_wu", off.idleWasteWu},
                    {"recompute_executed_wu", off.executedWasteWu},
                    {"recompute_total_wu", allOffWaste},
                    {"compfrr_recovery_waste_wu", recoveryWaste},
                    {"recovery_saved_wu", allOffWaste - recoveryWaste},
                    {"compfrr_total_waste_wu", normal * s.rate + recoveryWaste},
                    {"victim_net_saved_wu", allOffWaste - recoveryWaste - normal * s.rate},
                    {"deadline_time_ns", observed->deadlineTimeNs},
                    {"deadline_feasible_shadow",
                     shadow.seconds + in.work * (1 - in.progress) / in.rate <=
                         (observed->deadlineTimeNs - Now()) / static_cast<double>(SECOND)}};
        row["start_to_fault_s"] = s.startNs >= 0 ? Json((Now() - s.startNs) / 1e9) : Json();
        row["on_to_fault_s"] = s.onNs >= 0 ? Json((Now() - s.onNs) / 1e9) : Json();
        m_faults.push_back(row);
        s.summary.update({{"real_fault_type", source},
                          {"real_fault_time_ns", Now()},
                          {"shadow_mode_at_fault", before},
                          {"fault_progress", in.progress},
                          {"l_fault", l},
                          {"r_fault", r},
                          {"fault_classification", classification},
                          {"t_catch_shadow_s", shadow.seconds},
                          {"w_waste_shadow", normal * s.rate + recoveryWaste}});
    }
    Event(id, failed ? "REAL_COMPUTE_FAILED" : "REAL_COMPUTE_COMPLETE", {{"mode_before", before}});
    s.StopComputation(Now(), failed, observed != nullptr);
    if (s.nextTarget.IsPending())
        Simulator::Cancel(s.nextTarget);
    for (auto& event : s.events)
        if (event.IsPending())
            Simulator::Cancel(event);
}

void
ShadowEvaluator::Finalize()
{
    if (m_finalized)
        throw std::logic_error("shadow already finalized");
    m_finalized = true;
    std::vector<Json> tasks;
    for (auto& [id, s] : m_states)
    {
        Json row = s.summary;
        const int64_t end = s.stopNs >= 0 ? s.stopNs : Now();
        row.update(
            {{"final_shadow_mode", s.mode},
             {"real_task_state", TaskStateToString(m_real.at(id)->state)},
             {"on_time_s", s.onNs >= 0 ? (end - s.onNs) / 1e9 : 0.0},
             {"initialization_duration_attempted_s",
              s.startNs < 0 ? 0.0 : ((s.onNs >= 0 ? s.onNs : end) - s.startNs) / 1e9},
             {"N_L", s.localCount},
             {"N_R", s.remoteCount},
             {"delta_change_count", s.deltaChanges},
             {"n_change_count", s.nChanges},
             {"config_change_count", s.configChanges},
             {"normal_cost_s", s.NormalCost()},
             {"normal_waste_wu", s.NormalCost() * s.rate},
             {"peak_local_tail_bytes", s.peakLocalTailBytes},
             {"peak_local_materialized_state_bytes", s.peakLocalStateBytes},
             {"peak_remote_materialized_state_bytes", s.peakRemoteStateBytes},
             {"peak_replicated_input_bytes",
              s.onNs >= 0 ? m_real.at(id)->definition.inputBytes : 0},
             {"peak_pending_l1_count", s.peakPendingLocalCount},
             {"peak_remote_inflight_batches", s.peakRemoteInFlight},
             {"peak_initial_state_bytes", s.startNs < 0 ? 0 : s.layout.StateAt(s.initialWork)}});
        tasks.push_back(std::move(row));
    }
    WriteShadowCsv(m_output / "shadow-decisions.csv",
                   m_decisions,
                   {"time_ns",
                    "task_id",
                    "q_comp_1s",
                    "p_finish",
                    "j_off_s",
                    "j_start_s",
                    "j_on_s",
                    "best_delta",
                    "best_n",
                    "best_remote_interval",
                    "feasible_candidate_count",
                    "t_init_s"});
    WriteShadowCsv(m_output / "shadow-task-summary.csv",
                   tasks,
                   {"task_id",
                    "start_time_ns",
                    "start_progress",
                    "start_p_finish",
                    "start_q_1s",
                    "init_complete_time_ns",
                    "real_fault_type",
                    "real_fault_time_ns",
                    "shadow_mode_at_fault",
                    "fault_progress",
                    "l_fault",
                    "r_fault",
                    "fault_classification",
                    "t_catch_shadow_s",
                    "w_waste_shadow"});
    WriteShadowCsv(
        m_output / "shadow-faults.csv",
        m_faults,
        {"fault_id", "task_id", "fault_type", "fault_time", "primary_f1_f2", "mode_at_fault"});
    WriteShadowCsv(m_output / "shadow-events.csv", m_events, {"time_ns", "task_id", "event"});
    const Json assumptions = {
        {"kind", "shadow / analytical decision evaluation"},
        {"bandwidth_bytes_per_s", m_bandwidth},
        {"actual_packets_or_cpu_reservations", false},
        {"node_available", true},
        {"path_available", true},
        {"storage_capacity_binding", false},
        {"f3_in_primary_risk_or_recovery", false},
        {"normal_cost_population", "all tasks, including F3 victim"},
        {"initialization_legal_boundary", "greatest already completed G1 boundary"},
        {"future_target_anchor", "max(last triggered checkpoint WU, current completed WU)"},
        {"on_without_feasible_candidate",
         "retain state; pause new targets; finish in-flight operations"},
        {"same_time_order",
         "real pre-scheduled model/completion events precede later virtual events"},
        {"decision_grid",
         "compute START then every elapsed second, strictly before compute completion"},
        {"query_grid", "global fault checks in (now, now+horizon], excludes current draw"}};
    std::ofstream stream(m_output / "shadow-assumptions.json");
    stream << assumptions.dump(2) << '\n';
    if (!stream)
        throw std::runtime_error("cannot write shadow assumptions");
}
} // namespace ns3::compfrr
