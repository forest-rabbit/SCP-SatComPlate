/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-controller.h"
#include "input/input-cost-adapter.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
/** Same initialization contract as Frequency, evaluated on actual candidate resources. */
int64_t ReadyAfter(const FrequencyInput& in)
{
    const auto delay = InputCostAdapter(in.inputPolicy).InitializationSeconds(
        in.costs.localNs, in.costs.remoteNs, in.baseTransferSeconds, in.stateTransferSeconds);
    const long double ns = std::ceil(static_cast<long double>(delay) * 1e9L);
    if (!std::isfinite(ns) || ns < 0 || ns > std::numeric_limits<int64_t>::max() - in.risk.epochNs)
        throw std::logic_error("N5C ready estimate overflow");
    return in.risk.epochNs + static_cast<int64_t>(ns);
}
} // namespace

std::vector<N5cForecast> CompFrrController::N5cPeers(
    uint32_t remote, uint64_t excluded, const std::string& trigger, DecisionPathSnapshot& paths)
{
    std::vector<N5cForecast> peers;
    for (auto& [id, state] : m_states)
    {
        if (id == excluded || !state.pair || state.pair->remoteNode != remote) continue;
        const auto& task = Task(id);
        const auto inventory = m_manager.Inventory(id);
        if (task.state != TASK_RUNNING || task.attemptGeneration || !inventory || !inventory->active)
            continue;
        const auto primary = Service(task.definition.computeNodeId);
        const auto live = primary->GetRunningTaskSnapshot();
        if (!live || live->taskId != id) continue;
        auto prediction = m_faults->QueryTaskPrediction(task.definition.computeNodeId, live->remainingTimeNs);
        if (!prediction) continue;
        // All same-ns proposals precede fault application. Include this pending check for
        // every primary, regardless of the fault engine's node iteration order. No draw/read of outcomes.
        if (trigger == "FAULT_EPOCH") prediction->firstSampleTimeNs = prediction->predictionTimeNs;
        FrequencyDecisionRecord row;
        row.taskId = id;
        row.pair = state.pair;
        auto& in = row.input;
        in.phase = ProtectionPhase::ON;
        in.risk = MakeFrequencyRisk(CombineComputeFaultProbabilities(
            prediction->f1Model ? prediction->f1State.stepFailureProbability : 0,
            prediction->f2Model ? prediction->f2State.stepFailureProbability : 0), *prediction);
        in.primaryRate = primary->GetComputeRateWorkUnitsPerSecond();
        in.work = task.definition.computeWorkUnits;
        row.progressWork = static_cast<uint64_t>(std::min<unsigned __int128>(
            task.definition.computeWorkUnits,
            static_cast<unsigned __int128>(in.risk.epochNs - task.computeStartTimeNs) *
            primary->GetComputeRateWorkUnitsPerSecond() / 1000000000));
        in.progress = row.progressWork / in.work;
        in.remainingSeconds = live->remainingTimeNs / 1e9;
        in.deadlineNs = task.computeDeadlineTimeNs;
        in.inputBytes = task.definition.inputBytes;
        in.variableBytes = TaskStateAdapter(task.definition).VariableBytes();
        in.costs = GetProtectionCosts(static_cast<uint64_t>(in.variableBytes));
        if (!BuildResources(row, task, state, paths)) continue;
        // A pending initialization has no promised network-completion timestamp. Use a
        // conservative current full-init estimate, explicitly distinct from actual readiness.
        const auto ready = m_n5c->ReadyAfter(id).value_or(ReadyAfter(in));
        in.storageDemand = {};
        peers.push_back({id, task.definition.computeNodeId, std::move(in),
                        {inventory->config.deltaPermille, inventory->config.batchN}, ready,
                        m_tasks->IsSatelliteAvailable(state.pair->localNode) &&
                        m_tasks->IsComputeAvailable(state.pair->localNode),
                        task.definition.sourceNodeId == remote});
    }
    return peers;
}

void CompFrrController::SelectN5cRemote(
    FrequencyDecisionRecord& row, const TaskRuntime& task, State& state,
    DecisionPathSnapshot& paths, const std::vector<PlacementDecision>& pairs)
{
    const auto& policy = dynamic_cast<const N5cPlacementPolicy&>(*m_placement);
    N5cDecisionTrace trace;
    trace.taskId = row.taskId;
    trace.timeNs = row.input.risk.epochNs;
    trace.trigger = row.trigger;
    trace.reference = *row.pair;
    trace.referenceInput = row.input;
    trace.config = row.proposal.selected->config;
    const auto primary = Service(task.definition.computeNodeId)->GetRunningTaskSnapshot();
    if (!primary || primary->taskId != row.taskId || primary->remainingTimeNs < 0)
        throw std::logic_error("N5C missing current primary remaining compute time");
    for (const auto& pair : pairs)
    {
        if (pair.localNode != trace.reference.localNode) continue;
        auto actual = row;
        actual.pair = pair;
        actual.resourceReason.clear();
        N5cCandidate candidate;
        candidate.remoteNode = pair.remoteNode;
        m_n5c->FillResources(candidate, primary->remainingTimeNs);
        if (!BuildResources(actual, task, state, paths))
            candidate.rejection = actual.resourceReason;
        else if (!actual.input.nodeAvailable)
            candidate.rejection = "NODE_UNAVAILABLE";
        else
        {
            const auto storage = actual.input.storageDemand(trace.config);
            if (!storage || storage->localAdditionalBytes > actual.input.localFreeBytes)
                candidate.rejection = "LOCAL_STORAGE_INFEASIBLE";
            else candidate.additionalQuotaBytes = storage->remoteAdditionalBytes;
        }
        candidate.demand = {row.taskId, task.definition.computeNodeId, actual.input, trace.config,
                            0, actual.input.pathAvailable,
                            task.definition.sourceNodeId == pair.remoteNode};
        if (candidate.rejection.empty())
        {
            candidate.demand.readyAfterNs = ReadyAfter(actual.input);
            if ((candidate.demand.readyAfterNs - trace.timeNs) / 1e9 >= actual.input.remainingSeconds)
                candidate.rejection = "INITIALIZATION_TOO_LATE";
            candidate.propagationNs = paths.Get(task.definition.computeNodeId, pair.remoteNode).propagationNs;
            candidate.peers = N5cPeers(pair.remoteNode, row.taskId, row.trigger, paths);
        }
        trace.candidates.push_back(std::move(candidate));
    }
    trace.selection = policy.SelectRemote(trace.candidates);
    if (trace.selection.remoteNode)
    {
        row.pair = PlacementDecision{trace.reference.localNode, *trace.selection.remoteNode};
        const auto selected = std::find_if(trace.candidates.begin(), trace.candidates.end(),
            [&](const auto& c) { return c.remoteNode == *trace.selection.remoteNode; });
        row.n5cPeak = m_n5c->PeakFor(selected->remoteNode, row.taskId, selected->additionalQuotaBytes);
        row.localLoad = m_loads.Get(row.pair->localNode);
        row.remoteLoad = m_loads.Get(row.pair->remoteNode);
    }
    else
    {
        row.proposal.action = FrequencyAction::NONE;
        row.resourceReason = "N5C_NO_FEASIBLE_REMOTE";
        row.proposal.reason = row.resourceReason;
    }
    row.n5cTrace = m_n5c->Record(std::move(trace));
}

bool CompFrrController::RevalidateN5c(FrequencyDecisionRecord& row,
                                                  const TaskRuntime& task, State& state)
{
    // Same-ns peers may commit or fail after this proposal. Recheck only the fixed pair
    // and fixed configuration; never rank again, solve again, or allocate before the gate.
    DecisionPathSnapshot paths([this](auto source, auto destination) {
        return m_tasks->GetTransferEngine()->EstimateAdmissiblePath(source, destination);
    });
    auto actual = row;
    actual.resourceReason.clear();
    if (!BuildResources(actual, task, state, paths) || !actual.input.nodeAvailable)
    {
        row.resourceReason = actual.resourceReason.empty() ? "N5C_POST_BATCH_NODE_UNAVAILABLE" : actual.resourceReason;
        return false;
    }
    const auto storage = actual.input.storageDemand(row.proposal.selected->config);
    if (!storage || storage->localAdditionalBytes > actual.input.localFreeBytes)
    {
        row.resourceReason = "N5C_POST_BATCH_LOCAL_STORAGE";
        return false;
    }
    const auto peak = m_n5c->PeakFor(row.pair->remoteNode, row.taskId, storage->remoteAdditionalBytes);
    if (!m_n5c->CanCommit(row.taskId, row.pair->remoteNode, peak))
    {
        row.resourceReason = "N5C_POST_BATCH_QUOTA";
        return false;
    }
    N5cForecast forecast{row.taskId, task.definition.computeNodeId, actual.input,
                        row.proposal.selected->config, 0, true,
                        task.definition.sourceNodeId == row.pair->remoteNode};
    if (N5cCatchSeconds(forecast) > N5cBudgetSeconds(forecast, row.input.risk.epochNs) ||
        (row.proposal.action == FrequencyAction::START &&
         (ReadyAfter(actual.input) - row.input.risk.epochNs) / 1e9 >= actual.input.remainingSeconds))
    {
        row.resourceReason = "N5C_POST_BATCH_DEADLINE";
        return false;
    }
    row.n5cPeak = peak;
    return true;
}
} // namespace ns3::protection
