/* SPDX-License-Identifier: GPL-2.0-only */
#include "multitree-decision-log.h"
#include "ns3/simulator.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ns3::protection::multitree
{
DecisionLog::DecisionLog(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults)
    : m_tasks(tasks), m_faults(faults), m_scale(ReadCalibration(DefaultCalibrationPath()))
{
    if (!tasks || !faults) throw std::invalid_argument("Multi-tree needs tasks and online fault model");
    for (const auto& task : tasks->GetTaskRuntimes())
    {
        m_byId.emplace(task.definition.taskId, &task);
        m_bytes.emplace(task.definition.taskId, task.definition.inputBytes);
    }
}

RuleResult DecisionLog::Capture(uint64_t id)
{
    if (m_rows.contains(id)) return m_rows.at(id).rule;
    const auto& task = *m_byId.at(id);
    if (task.state != TASK_RUNNING)
        throw std::logic_error("Multi-tree decision must occur at primary TASK_RUNNING");
    for (const auto& service : m_tasks->GetComputeServices())
    {
        if (service->GetNodeId() != task.definition.computeNodeId) continue;
        const auto running = service->GetRunningTaskSnapshot();
        if (!running || running->taskId != id || running->remainingTimeNs <= 0)
            throw std::logic_error("Multi-tree missing current primary service");
        const auto prediction = m_faults->QueryTaskPrediction(service->GetNodeId(),
                                                             running->remainingTimeNs);
        if (!prediction) throw std::logic_error("Multi-tree current probability unavailable");
        auto features = MapFeatures(task, service->GetQueuedTaskIds(), m_bytes, m_scale, *prediction);
        const auto rule = Evaluate(features);
        m_rows.emplace(id, Row{std::move(features), rule, Simulator::Now().GetNanoSeconds()});
        return rule;
    }
    throw std::logic_error("Multi-tree primary service not found");
}

Decision DecisionLog::Get(uint64_t id) const
{
    const auto row = m_rows.find(id);
    return row == m_rows.end() ? Decision::UNDECIDED : row->second.rule.decision;
}

void DecisionLog::Write(const std::filesystem::path& directory) const
{
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "multitree-decisions.csv");
    csv << std::setprecision(17);
    csv << "task_id,profile,primary_node,decision_time_ns,input_bytes,compute_deadline_budget_ns,"
           "input_percentile,deadline_percentile,ts_mt,iddl_mt,queued_count,queued_ids,cl_mt,"
           "q_f1,q_f2,q_comp,check_interval_ns,fr_mt,decision,branch\n";
    nlohmann::json summary = {{"task_count", m_byId.size()}, {"RS", 0}, {"RP", 0},
        {"UNDECIDED", m_byId.size() - m_rows.size()}, {"branches", nlohmann::json::object()},
        {"profiles", nlohmann::json::object()}, {"undecided_task_ids", nlohmann::json::array()}};
    for (const auto& [id, taskPtr] : m_byId)
    {
        if (!m_rows.contains(id))
        {
            summary["undecided_task_ids"].push_back(id);
            continue;
        }
        const auto& row = m_rows.at(id);
        const auto& f = row.features;
        const auto& task = *taskPtr;
        const auto name = ToString(row.rule.decision);
        const auto profile = TaskProfileToString(task.definition.taskProfile);
        summary[name] = summary[name].get<size_t>() + 1;
        auto& branch = summary["branches"][row.rule.branch];
        branch = branch.is_null() ? 1 : branch.get<size_t>() + 1;
        auto& count = summary["profiles"][profile][name];
        count = count.is_null() ? 1 : count.get<size_t>() + 1;
        csv << id << ',' << profile << ',' << task.definition.computeNodeId << ',' << row.timeNs
            << ',' << task.definition.inputBytes << ',' << task.computeDeadlineBudgetNs << ','
            << f.inputPercentile << ',' << f.deadlinePercentile << ',' << f.ts << ',' << f.iddl
            << ',' << f.queuedIds.size() << ',';
        for (size_t j = 0; j < f.queuedIds.size(); ++j)
            csv << (j ? ";" : "") << f.queuedIds[j];
        csv << ',' << f.cl << ',' << f.qF1 << ',' << f.qF2 << ',' << f.qComp << ','
            << f.checkIntervalNs << ',' << f.fr << ',' << name << ',' << row.rule.branch << '\n';
    }
    std::ofstream json(directory / "multitree-mapping-summary.json");
    json << summary.dump(2) << '\n';
    if (!csv || !json) throw std::runtime_error("Multi-tree decision audit write failed");
}
} // namespace ns3::protection::multitree
