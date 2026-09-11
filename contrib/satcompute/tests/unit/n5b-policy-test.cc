/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/backup-storage-pool.h"
#include "ns3/placement-load-ledger.h"
#include "ns3/checkpoint-progress.h"
#include "ns3/compfrr-frequency-policy.h"
#include "ns3/compfrr-shadow-model.h" // Independent test oracle only.
#include "ns3/compute-fault-combination.h"
#include "ns3/first-feasible-placement-policy.h"
#include "ns3/fixed-protection-policy.h"
#include "ns3/frequency-decision-gate.h"
#include "ns3/least-recovery-load-placement-policy.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;

namespace
{
uint64_t checks{};

void Check(bool condition, const char* label)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(label);
}

void Near(double actual, double expected, const char* label)
{
    Check(std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected)), label);
}

template <class F> void Reject(F function)
{
    try
    {
        function();
    }
    catch (const std::invalid_argument&)
    {
        ++checks;
        return;
    }
    throw std::runtime_error("invalid N5B input accepted");
}

/** Independent test-only copy of the pre-extraction minimum-ID pair rule. */
std::optional<PlacementDecision> LegacyPair(const PlacementContext& c)
{
    std::optional<uint32_t> local, remote;
    for (const auto& a : c.candidates)
        if (a.nodeId != c.primaryNode && a.healthy && a.idle && a.reachable && a.oneHop &&
            (!local || a.nodeId < *local))
            local = a.nodeId;
    if (!local)
        return std::nullopt;
    for (const auto& a : c.candidates)
        if (a.nodeId != c.primaryNode && a.nodeId != *local && a.healthy && a.idle && a.reachable &&
            (!remote || a.nodeId < *remote))
            remote = a.nodeId;
    return remote ? std::optional(PlacementDecision{*local, *remote}) : std::nullopt;
}

void PlacementChecks()
{
    FirstFeasiblePlacementPolicy ffp;
    PlacementContext c{2, {{3}, {0}, {2}, {1}}};
    for (uint32_t mask = 0; mask < 65536; ++mask)
    {
        for (size_t i = 0; i < c.candidates.size(); ++i)
        {
            auto& a = c.candidates[i];
            const auto flags = mask >> (4 * i);
            a.healthy = flags & 1;
            a.idle = flags & 2;
            a.reachable = flags & 4;
            a.oneHop = flags & 8;
        }
        const auto expected = LegacyPair(c);
        Check(ffp.SelectCheckpointPair(c) == expected, "FFP differs from N5A pair rule");
        const auto ranked = LeastRecoveryLoadPlacementPolicy(1).SelectCheckpointPair(c);
        Check(bool(ranked) == bool(expected), "LRL/FFP hard-feasible sets differ");
        if (ranked)
        {
            const auto get = [&](uint32_t node) -> const BackupCandidate& {
                return *std::find_if(c.candidates.begin(), c.candidates.end(),
                                     [node](const auto& a) { return a.nodeId == node; });
            };
            Check(IsPlacementCandidate(get(ranked->localNode), c.primaryNode) &&
                      get(ranked->localNode).oneHop &&
                      IsPlacementCandidate(get(ranked->remoteNode), c.primaryNode) &&
                      ranked->localNode != ranked->remoteNode,
                  "LRL violated shared hard feasibility");
        }
        FixedProtectionPolicy fixed(50, 4);
        ProtectionContext context;
        context.attempt = {1, 0};
        context.primaryNode = c.primaryNode;
        context.firstComputeStart = context.taskSelected = true;
        context.candidates = c.candidates;
        const auto action = fixed.OnTaskComputeStart(context);
        Check(bool(action.checkpoint) == bool(expected), "fixed extraction changed admission");
        if (expected)
            Check(action.checkpoint->localNode == expected->localNode &&
                      action.checkpoint->remoteNode == expected->remoteNode,
                  "fixed extraction changed pair");
    }
    c = {10,
         {{3, true, true, true, true},
          {0, true, true, true, true},
          {2, true, true, true, true},
          {1, true, true, true, true}}};
    const auto expected = ffp.SelectCheckpointPair(c);
    c.candidates[1].backupAssignmentCount = 100000;
    c.candidates[1].storageFreeBytes = 0;
    Check(ffp.SelectCheckpointPair(c) == expected, "FFP must not silently become load/storage balancing");
    std::reverse(c.candidates.begin(), c.candidates.end());
    Check(ffp.SelectCheckpointPair(c) == expected, "stable ID not input order");

    LeastRecoveryLoadPlacementPolicy lrl(2);
    Check(ffp.SelectBackupNode(c) == 0 && lrl.SelectBackupNode(c) != 0,
          "single-node role must use injected ranking, without a fake pair");
    Check(ffp.SelectBackupNode(c, [](auto n) { return n == 3; }) == 3,
          "operation feasibility ignored");
    Check(!ffp.SelectBackupNode(c, [](auto) { return false; }), "infeasible single node admitted");
    PlacementContext single{10, {{1, true, true, true, false}}};
    Check(ffp.SelectBackupNode(single) == 1 && !ffp.SelectCheckpointPair(single),
          "single backup incorrectly requires one-hop or a second node");
    for (auto member : {&BackupCandidate::healthy, &BackupCandidate::idle, &BackupCandidate::reachable})
    {
        single.candidates[0].*member = false;
        Check(!ffp.SelectBackupNode(single), "single role missed shared hard filter");
        single.candidates[0].*member = true;
    }
    single.primaryNode = 1;
    Check(!ffp.SelectBackupNode(single), "single backup may not be its primary");
    ProtectionContext fixedContext;
    fixedContext.attempt = {100, 0};
    fixedContext.primaryNode = c.primaryNode;
    fixedContext.firstComputeStart = fixedContext.taskSelected = true;
    fixedContext.candidates = c.candidates;
    FixedProtectionPolicy injected(50, 4, std::make_unique<LeastRecoveryLoadPlacementPolicy>(2));
    const auto injectedPair = injected.OnTaskComputeStart(fixedContext).checkpoint;
    const auto rankedPair = lrl.SelectCheckpointPair(c);
    Check(injectedPair && injectedPair->localNode == rankedPair->localNode &&
              injectedPair->remoteNode == rankedPair->remoteNode &&
              injectedPair->localNode != ffp.SelectCheckpointPair(c)->localNode,
          "fixed still hardcodes FFP instead of executing injected LRL");
    Check(lrl.SelectCheckpointPair(c)->localNode != 0, "LRL avoids synthetic concentration");
    for (auto& a : c.candidates)
        a.backupAssignmentCount = 0;
    for (int task = 0; task < 40; ++task)
    {
        const auto pair = *lrl.SelectCheckpointPair(c);
        for (auto& a : c.candidates)
            a.backupAssignmentCount += a.nodeId == pair.remoteNode;
    }
    for (const auto& a : c.candidates)
        // Local-first keeps node 0 as local here; only the other three can accumulate remote load.
        Check(a.backupAssignmentCount == (a.nodeId == 0 ? 0 : a.nodeId == 1 ? 14 : 13),
              "LRL remote-only synthetic assignment distribution");
    for (auto& a : c.candidates)
        a.backupAssignmentCount = a.nodeId == 0 ? std::numeric_limits<uint64_t>::max() : 0;
    c.candidates[0].activeRecoveryCount = std::numeric_limits<uint64_t>::max();
    Check(lrl.SelectCheckpointPair(c)->localNode != c.candidates[0].nodeId, "LRL weighted count overflow");

    PlacementLoadLedger loads;
    loads.RegisterNode(0);
    loads.RegisterNode(1);
    loads.Assignment(1, 0, true, 0);
    loads.Assignment(2, 0, true, 1);
    loads.Assignment(1, 0, false, 2);
    loads.Assignment(1, 0, false, 2);
    Check(loads.Get(0).activeBackup == 1 && loads.Get(0).totalBackup == 2 &&
          loads.Get(0).peakBackup == 2, "assignment current/total/release mismatch");
    loads.Recovery(1, 1, true, 3);
    Check(loads.Get(1).activeRecovery == 1 && loads.Get(1).totalRecovery == 1,
          "accepted recovery not counted");
    loads.Recovery(1, 1, false, 4);
    loads.Recovery(1, 1, false, 4);
    loads.Assignment(2, 0, false, 4);
    Check(loads.Empty() && loads.Get(1).peakRecovery == 1, "load counters leak/underflow");
}

void FeasiblePairChecks()
{
    PlacementContext c{3, {{0, true, true, true, true}, {1, true, true, true, true},
                           {2, true, true, true, false}, {3, true, true, true, true}}};
    FirstFeasiblePlacementPolicy ffp;
    LeastRecoveryLoadPlacementPolicy lrl(1);
    uint64_t probes = 0;
    auto probe = [&](uint32_t s, uint32_t d) {
        ++probes;
        const bool blocked = s == 0 && d == 1;
        return PlacementPathAvailability{true, !blocked, blocked ? "NO_ADMISSIBLE_PATH" : ""};
    };
    auto set = BuildFeasiblePlacementPairs(c, probe);
    Check(set.total == 6 && set.nodeFeasible == 4 && set.pairs.size() == 3 &&
          set.skipNoCapacity == 1 && set.skipNode == 2, "pair counts/node/path filters wrong");
    Check(probes <= 7, "shared same-time path probes were not memoized");
    auto a = set.pairs, b = set.pairs;
    ffp.RankPairs(a, c);
    lrl.RankPairs(b, c);
    Check(a.front() == PlacementDecision{0, 2} && b.front() == PlacementDecision{0, 2},
          "blocked first pair prevented trying second feasible pair");
    c.candidates[0].backupAssignmentCount = 8;
    lrl.RankPairs(b, c);
    Check(b.front() == PlacementDecision{1, 2}, "LRL ranking changed common eligibility");
    ffp.RankPairs(b, c);
    Check(a == b, "policies did not consume identical feasible sets");
    std::reverse(c.candidates.begin(), c.candidates.end());
    auto reversed = BuildFeasiblePlacementPairs(c, probe).pairs;
    ffp.RankPairs(reversed, c);
    Check(reversed == a, "pair input order changed deterministic feasible set");
    for (const auto& blocked : {std::pair{3u,0u}, std::pair{3u,2u}, std::pair{0u,2u}})
    {
        auto result = BuildFeasiblePlacementPairs(c, [&](auto s, auto d) {
            const bool route = std::pair{s,d} != blocked;
            return PlacementPathAvailability{route, route, route ? "" : "NO_ROUTE"};
        });
        Check(std::find(result.pairs.begin(), result.pairs.end(), PlacementDecision{0,2}) == result.pairs.end(),
              "required primary/local/remote route was not hard checked");
    }
    auto capacity = BuildFeasiblePlacementPairs(c, [](auto,auto) {
        return PlacementPathAvailability{true,false,"NO_ADMISSIBLE_PATH"};
    });
    Check(capacity.reason == "NO_CAPACITY_NOW", "temporary exhaustion misclassified");
    auto disconnected = BuildFeasiblePlacementPairs(c, [](auto,auto) {
        return PlacementPathAvailability{false,false,"NO_ROUTE"};
    });
    Check(disconnected.reason == "NO_ROUTE", "no topology route misclassified");
    for (auto& node : c.candidates)
    {
        node.healthy = node.nodeId != 0;
        node.idle = node.nodeId != 1;
    }
    Check(BuildFeasiblePlacementPairs(c, probe).reason == "NO_FEASIBLE_NODE_PAIR",
          "unhealthy/busy/non-one-hop nodes admitted");
}

/** Explicit unbounded scalar oracle fixture, not a production resource adapter. */
FrequencyInput Toy()
{
    FrequencyInput in;
    in.risk = {10000000000, 1000000000, .2, .8};
    in.inputBytes = 1000000000;
    in.work = 1500000;
    in.variableBytes = 500000000;
    in.progress = .6;
    in.primaryRate = in.recoveryRate = 100000;
    in.inputBandwidth = in.backupBandwidth = 1250000000;
    in.remainingSeconds = 6;
    in.deadlineNs = 20500000000;
    in.costs = GetProtectionCosts(500000000);
    in.baseTransferSeconds = .8;
    in.stateTransferSeconds = .24;
    in.nodeAvailable = in.pathAvailable = true;
    in.localFreeBytes = in.remoteFreeBytes = std::numeric_limits<uint64_t>::max();
    in.storageDemand = [](auto) { return FrequencyStorageDemand{}; };
    return in;
}

void Oracle(const FrequencyInput& in, const char* label)
{
    const auto actual = CompFrrFrequencyPolicy().Evaluate(in);
    const bool on = in.phase == ProtectionPhase::ON;
    compfrr::DecisionInput oracle;
    oracle.inputBytes = in.inputBytes;
    oracle.work = in.work;
    oracle.variableBytes = in.variableBytes;
    oracle.rate = in.primaryRate;
    oracle.bandwidth = in.backupBandwidth;
    oracle.progress = in.progress;
    oracle.remainingSeconds = in.remainingSeconds;
    oracle.deadlineSlack = actual.deadlineSlackSeconds;
    oracle.qOneSecond = in.risk.qCurrentSample;
    oracle.pFinish = in.risk.pFailBeforeFinish;
    oracle.costs = {in.costs.localNs / 1e9, in.costs.remoteNs / 1e9, 0};
    oracle.nodeAvailable = in.nodeAvailable;
    oracle.pathAvailable = in.pathAvailable;
    const auto expected = compfrr::SelectFrequency(oracle, on);
    Check(bool(actual.selected) == bool(expected.best), "oracle feasible result");
    Check(actual.feasibleCount == expected.feasibleCount, "oracle feasible count");
    Near(*actual.jOff,
         in.risk.pFailBeforeFinish * compfrr::RecomputeCatchUp(oracle),
         "oracle J_OFF");
    if (actual.selected)
    {
        const auto& a = *actual.selected;
        const auto& b = *expected.best;
        Check(a.config.deltaPermille == uint32_t(b.deltaPermille) &&
                  a.config.batchN == uint32_t(b.remoteEvery),
              "oracle optimum delta/n");
        Near(a.objective, b.objective, "oracle candidate J");
        Near(a.averageRecoverySeconds, b.averageCatchUp, "oracle Rbar");
        if (!on)
        {
            Near(*actual.jStart,
                 oracle.costs.local + oracle.costs.remote + b.objective,
                 "oracle J_START");
            Check(
                (actual.action == FrequencyAction::START) ==
                    compfrr::ShouldStartProtection(oracle, expected, actual.initializationSeconds),
                "oracle START strict comparison");
        }
        if (label)
            std::cout << label << ": J=" << a.objective << " Rbar=" << a.averageRecoverySeconds
                      << " feasible=" << actual.feasibleCount
                      << " delta_permille=" << a.config.deltaPermille << " n=" << a.config.batchN
                      << " (matches oracle)\n";
    }
}

void SolverChecks()
{
    auto unavailable = Toy();
    unavailable.replayAvailable = false;
    unavailable.inputBandwidth = 0;
    const auto protectWithoutReplay = CompFrrFrequencyPolicy().Evaluate(unavailable);
    Check(!protectWithoutReplay.jOff && protectWithoutReplay.action == FrequencyAction::START,
          "unavailable OFF input incorrectly vetoed executable protection");
    unavailable.risk.pFailBeforeFinish = 0;
    const auto noRisk = CompFrrFrequencyPolicy().Evaluate(unavailable);
    Check(noRisk.jOff == 0 && noRisk.action == FrequencyAction::NONE,
          "unavailable OFF input forced zero-risk protection");
    CompFrrFrequencyPolicy policy;
    auto in = Toy();
    Oracle(in, "OFF/START anchor");
    const auto start = policy.Evaluate(in);
    Near(*start.jOff, 7.84, "hand OFF");
    Near(*start.jStart, .1049, "hand START");
    Near(start.initializationSeconds, .802, "cL/base concurrent initialization");
    Check(start.action == FrequencyAction::START, "high P starts");
    in.phase = ProtectionPhase::ON;
    Oracle(in, "ON high-q anchor");
    Near(policy.Evaluate(in).selected->objective, .022888888888888893, "hand ON");
    const auto high = policy.Evaluate(in);
    in.risk.qCurrentSample = 0;
    Oracle(in, "ON zero-q anchor");
    const auto low = policy.Evaluate(in);
    Check(low.selected->config == FrequencyConfiguration{100, 10}, "zero q low maintenance");
    Check(high.selected->averageRecoverySeconds < low.selected->averageRecoverySeconds,
          "controlled high q increases protection strength");
    in.costs = {0, 0}; // Synthetic exact tie, not a second production cost tier.
    Oracle(in, "exact tie anchor");
    Check(policy.Evaluate(in).selected->config == FrequencyConfiguration{10, 1},
          "exact tie smaller delta then n");
    in.phase = ProtectionPhase::OFF;
    in.risk.pFailBeforeFinish = 0;
    Check(policy.Evaluate(in).action == FrequencyAction::NONE, "exact START tie stays OFF");
    in = Toy();
    in.risk.pFailBeforeFinish = 0;
    Check(policy.Evaluate(in).action == FrequencyAction::NONE, "zero P never START");
    in = Toy();
    in.baseTransferSeconds = in.remainingSeconds - in.costs.remoteNs / 1e9;
    Check(policy.Evaluate(in).action == FrequencyAction::NONE, "Tinit=Trem rejected");

    for (auto phase : {ProtectionPhase::OFF, ProtectionPhase::ON})
        for (double p : {0., .001, .1, .5, 1.})
            for (double x : {0., .2, .6, .95})
                for (uint64_t bytes : {100000000ULL, 100000001ULL, 500000000ULL, 500000001ULL})
                {
                    auto sample = Toy();
                    sample.phase = phase;
                    sample.progress = x;
                    sample.remainingSeconds = (1 - x) * sample.work / sample.primaryRate;
                    sample.risk.qCurrentSample = sample.risk.pFailBeforeFinish = p;
                    sample.variableBytes = bytes;
                    sample.costs = GetProtectionCosts(bytes);
                    Oracle(sample, nullptr);
                }
    in = Toy();
    in.phase = ProtectionPhase::ON;
    // x=0.6 leaves 6 s, so deadline 16.075 has exactly 0.075 s nominal slack.
    in.deadlineNs = 16075000000;
    Oracle(in, "Rmax boundary anchor");
    in.deadlineNs = 10000000000;
    Check(!policy.Evaluate(in).selected, "negative Rmax rejected");
    in = Toy();
    in.nodeAvailable = false;
    Check(!policy.Evaluate(in).selected, "unavailable node rejected");
    in.nodeAvailable = true;
    in.pathAvailable = false;
    Check(!policy.Evaluate(in).selected, "unreachable path rejected");

    in = Toy();
    std::set<uint32_t> deltaGrid, nGrid;
    in.storageDemand = [&](FrequencyConfiguration c) {
        Check(c.deltaPermille >= 10 && c.deltaPermille <= 100 && c.batchN >= 1 && c.batchN <= 100 &&
                  c.batchN * c.deltaPermille <= 1000,
              "complete legal grid");
        deltaGrid.insert(c.deltaPermille);
        nGrid.insert(c.batchN);
        return FrequencyStorageDemand{100, 200};
    };
    BackupStoragePool local(120), remote(230);
    local.Allocate(1, StorageKind::LOCAL_RECORD, 20);
    remote.TryReserve(2, StorageKind::REMOTE_STATE, 30);
    in.localFreeBytes = local.Free();
    in.remoteFreeBytes = remote.Free();
    Check(bool(policy.Evaluate(in).selected), "actual free headroom inclusive");
    Check(deltaGrid.size() == 91 && nGrid.size() == 100, "full search range including n*delta=1");
    --in.localFreeBytes;
    Check(!policy.Evaluate(in).selected, "local additional peak exceeds headroom");
    ++in.localFreeBytes;
    --in.remoteFreeBytes;
    Check(!policy.Evaluate(in).selected, "remote merge peak exceeds headroom");
    in.phase = ProtectionPhase::ON;
    Check(policy.Evaluate(in).action == FrequencyAction::PAUSE, "ON infeasible pauses not OFF");
    in = Toy();
    in.risk.intervalNs = 2000000000;
    in.phase = ProtectionPhase::ON;
    const auto two = policy.Evaluate(in);
    const auto cfg = two.selected->config;
    const double delta = cfg.deltaPermille / 1000.;
    Near(two.selected->normalSeconds,
         2 * in.primaryRate / in.work *
             (in.costs.localNs / 1e9 / delta + in.costs.remoteNs / 1e9 / (cfg.batchN * delta)),
         "cost interval follows supplied probability window");
    in = Toy();
    in.recoveryRate = 200000;
    in.inputBandwidth /= 2;
    Near(*policy.Evaluate(in).jOff, .8 * (1.6 + 4.5), "independent input path and recovery rate");
    Near(policy.Evaluate(in).deadlineSlackSeconds, 7.5, "Rmax uses recovery rate");
    in = Toy();
    in.storageDemand = {};
    Reject([&] { policy.Evaluate(in); });
    in = Toy();
    in.risk.qCurrentSample = std::numeric_limits<double>::quiet_NaN();
    Reject([&] { policy.Evaluate(in); });
    in = Toy();
    in.risk.qCurrentSample = 1.01;
    Reject([&] { policy.Evaluate(in); });
    for (auto phase :
         {ProtectionPhase::INITIALIZING, ProtectionPhase::RECOVERING, ProtectionPhase::DONE})
    {
        in.phase = phase;
        Check(policy.Evaluate(in).action == FrequencyAction::NONE, "inactive phases do not decide");
    }
}

void ProbabilityChecks()
{
    const auto parameters = GetDefaultFaultParameters();
    F1SelfStateFaultModel f1(parameters.f1);
    F2RadiationFaultModel f2(parameters.f2);
    ComputeFailurePredictionInput input;
    input.f1Model = &f1;
    input.f1State = f1.CreateInitialSnapshot();
    f1.Update(input.f1State, true, 25, 1);
    input.f2Model = &f2;
    input.f2State = f2.CreateInitialSnapshot();
    input.f2State.stepFailureProbability = .02; // Controlled current sample, future outside region.
    input.f2PositionAtTime = [](int64_t) { return Vector(0, 0, 7000000); };
    input.predictionTimeNs = 10000000000;
    input.remainingComputeTimeNs = 2000000000;
    input.checkIntervalNs = 1000000000;
    const auto before = input.f1State.temperatureC;
    const auto prediction = PredictComputeFailureBeforeFinish(input);
    const double q = CombineComputeFaultProbabilities(input.f1State.stepFailureProbability, .02);
    const auto risk = MakeFrequencyRisk(q, input);
    Check(risk.qCurrentSample == q &&
              prediction.steps.front().targetTimeNs == input.predictionTimeNs,
          "policy takes CURRENT actual sample probability");
    Check(prediction.steps.size() == 3, "canonical inclusive endpoint unchanged");
    Check(prediction.steps[1].combinedStepFailureProbability != q, "current and next q differ");
    Reject([&] { MakeFrequencyRisk(prediction.steps[1].combinedStepFailureProbability, input); });
    double survival = 1;
    for (const auto& step : prediction.steps)
        survival *= 1 - step.combinedStepFailureProbability;
    Near(risk.pFailBeforeFinish, 1 - survival, "current-inclusive canonical horizon");
    Check(input.f1State.temperatureC == before, "predictor never mutates live state");
    for (double a : {0., .999})
        for (double b : {0., .999})
        {
            const auto sampled =
                EvaluateComputeFaultSources(input.f1State.stepFailureProbability, a, .02, b);
            Check(sampled.combinedProbability == risk.qCurrentSample,
                  "same q as real source sampler");
            Check(sampled.computeFaultOccurred == (sampled.f1Occurred || sampled.f2Occurred),
                  "F1/F2 still independently sampled");
        }
    // The production predictor/input API has no F3 or future sampled-event field.
    const auto unchanged = MakeFrequencyRisk(q, input);
    Check(unchanged.pFailBeforeFinish == risk.pFailBeforeFinish,
          "causal risk construction is deterministic and read-only");
    for (int64_t remaining : {0LL, 1LL, 999999999LL, 1000000000LL, 1000000001LL})
    {
        input.remainingComputeTimeNs = remaining;
        const auto actual = MakeFrequencyRisk(q, input);
        Check(actual.pFailBeforeFinish ==
                  PredictComputeFailureBeforeFinish(input).predictedFailureProbability,
              "canonical integer horizon not duplicated");
    }
}

TaskDefinition Task(TaskProfile profile)
{
    TaskDefinition task;
    task.taskId = 123;
    task.taskProfile = profile;
    task.inputBytes = profile == TaskProfile::LLM ? 400 : 1000000000;
    task.computeWorkUnits = profile == TaskProfile::LLM ? 500000 : 1500000;
    return task;
}

void GateChecks()
{
    auto in = Toy();
    auto start = CompFrrFrequencyPolicy().Evaluate(in);
    FrequencyDecisionGate hit;
    hit.Propose(start);
    Check(hit.Phase() == ProtectionPhase::OFF && !hit.CurrentConfig(), "START only proposed");
    Check(!hit.Resolve(start.epochNs, true, false) && hit.Phase() == ProtectionPhase::OFF,
          "same-epoch fault sees OFF and prevents START");
    Reject([&] { hit.Resolve(start.epochNs, false, true); });
    Reject([&] { hit.Propose(start); });

    FrequencyDecisionGate retry;
    auto none = start;
    none.action = FrequencyAction::NONE;
    none.selected.reset();
    retry.Propose(none);
    Check(!retry.Resolve(none.epochNs, false, true), "blocked dispatch stays OFF");
    Reject([&] { retry.Propose(start); });
    retry.Propose(start, true);
    Check(retry.Resolve(start.epochNs, false, true), "same-ns resource release permits one fresh OFF retry");
    Reject([&] { retry.Propose(start, true); });
    FrequencyDecisionGate stillBlocked;
    stillBlocked.Propose(none, true);
    Check(!stillBlocked.Resolve(none.epochNs, false, true), "capacity interest is not START");
    Reject([&] { stillBlocked.Propose(none, true); });

    FrequencyDecisionGate gate;
    gate.Propose(start);
    Check(gate.Resolve(start.epochNs, false, true), "survive permits START commit");
    Check(gate.Phase() == ProtectionPhase::INITIALIZING, "START is not physical ON");
    gate.InitializationCommitted();
    const auto old = gate.CurrentConfig();
    in.phase = ProtectionPhase::ON;
    in.risk.epochNs += 1000000000;
    auto update = CompFrrFrequencyPolicy().Evaluate(in);
    update.selected->config = {80, 8};
    gate.Propose(update);
    Check(gate.CurrentConfig() == old, "proposed update cannot rewrite current config");
    Check(!gate.Resolve(update.epochNs, true, true) && gate.CurrentConfig() == old,
          "fault blocks frequency update even before liveness callback");
    ++update.epochNs;
    gate.Propose(update);
    Check(!gate.Resolve(update.epochNs, false, false), "completed primary blocks late commit");
    ++update.epochNs;
    gate.Propose(update);
    Check(gate.Resolve(update.epochNs, false, true), "future config after survival");
    Check(gate.NewBatchRecordCount(5) == 0, "n=8 retains five unbatched records");
    ++update.epochNs;
    update.selected->config = {50, 4};
    gate.Propose(update);
    gate.Resolve(update.epochNs, false, true);
    const auto immutableBatchCount = gate.NewBatchRecordCount(5);
    Check(immutableBatchCount == 4, "smaller n uses four pending records, leaves one");
    ++update.epochNs;
    update.selected->config = {80, 8};
    gate.Propose(update);
    gate.Resolve(update.epochNs, false, true);
    Check(immutableBatchCount == 4 && gate.NewBatchRecordCount(3) == 0,
          "created batch unchanged, unbatched three wait for new n=8");
    for (auto profile : {TaskProfile::DENSE_IMAGE,
                         TaskProfile::SPARSE_INFERENCE,
                         TaskProfile::COMPRESSION,
                         TaskProfile::LLM})
    {
        TaskStateAdapter layout(Task(profile));
        const auto current = layout.Work() * 6 / 10;
        const auto triggered = layout.Floor(layout.Work() / 10);
        const auto target = gate.NextTarget(layout, current, triggered);
        Check(target && *target > current && layout.Floor(*target) == *target,
              "four profiles forward-only legal target, no historical catch-up generation");
        if (profile == TaskProfile::LLM)
            Check(*target % 100 == 0, "whole-token checkpoint");
        Check(!gate.NextTarget(layout, layout.Work(), triggered), "no checkpoint after finish");
    }
    TaskStateAdapter layout(Task(TaskProfile::DENSE_IMAGE));
    CheckpointProgress progress(layout, 0, 0);
    const auto costs = GetProtectionCosts(layout.VariableBytes());
    const auto commitAt = progress.ReceiveInitialization(1, costs.localNs);
    progress.CommitRemote(commitAt);
    const auto prior = progress.BeforeFault(update.epochNs);
    auto pause = update;
    pause.epochNs++;
    pause.action = FrequencyAction::PAUSE;
    pause.selected.reset();
    gate.Propose(pause);
    gate.Resolve(pause.epochNs, false, true);
    Check(gate.Phase() == ProtectionPhase::ON && gate.Paused() && gate.CurrentConfig(),
          "infeasible epoch stays ON and retains committed config");
    Check(!gate.NextTarget(layout, 100000, 0) && gate.NewBatchRecordCount(100) == 0,
          "pause BOTH generation and new batching");
    Check(progress.Current().initialized == prior.initialized &&
              progress.Current().remoteWork == prior.remoteWork,
          "policy gate never rewrites real committed state");
    update.epochNs = pause.epochNs + 1;
    gate.Propose(update);
    gate.Resolve(update.epochNs, false, true);
    Check(!gate.Paused() && gate.NewBatchRecordCount(8) == 8, "resume consumes retained pending");
    Reject([&] { gate.Stop(ProtectionPhase::OFF); });
    gate.Stop(ProtectionPhase::RECOVERING);
    Check(!gate.NextTarget(layout, 100000, 0), "recovery stops primary policy");
    gate.Stop(ProtectionPhase::DONE);
    Reject([&] { gate.Stop(ProtectionPhase::RECOVERING); });
    Reject([&] { gate.InitializationCommitted(); });
}
} // namespace

int main()
{
    try
    {
        PlacementChecks();
        FeasiblePairChecks();
        SolverChecks();
        ProbabilityChecks();
        GateChecks();
        std::cout << "N5B policy: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
