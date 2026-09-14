/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/n5c-placement-policy.h"
#include "ns3/backup-storage-pool.h"
#include "ns3/frequency-decision-gate.h"
#include "ns3/compute-service.h"
#include "ns3/compute-usage-history.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ns3;
using namespace ns3::protection;
namespace
{
uint64_t checks{};
constexpr int64_t S = 1000000000;
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) throw std::runtime_error(name);
}
void Near(double actual, double expected, const char* name)
{
    Check(std::abs(actual - expected) < 1e-12, name);
}
template <class F> void Reject(F f)
{
    try { f(); } catch (const std::exception&) { ++checks; return; }
    throw std::runtime_error("invalid N5C input accepted");
}
N5cForecast Forecast(uint64_t task = 1, uint32_t primary = 1)
{
    N5cForecast f;
    f.taskId = task; f.primaryNode = primary; f.config = {50, 4};
    auto& in = f.input;
    in.work = 1000; in.progress = 0.2; in.primaryRate = in.recoveryRate = 100;
    in.variableBytes = in.inputBytes = in.backupBandwidth = in.inputBandwidth = 1000;
    in.costs.remoteNs = S / 100; in.deadlineNs = 20 * S;
    in.risk.epochNs = S; in.risk.intervalNs = S;
    in.risk.futureSteps = {{2 * S, .5}};
    f.readyAfterNs = S;
    return f;
}
N5cCandidate Candidate(uint32_t remote = 10)
{
    N5cCandidate c;
    c.remoteNode = remote; c.demand = Forecast(); c.capacityBytes = 1000;
    c.additionalQuotaBytes = 100; c.exposureNs = 10 * S; c.normalBusyNs = 2 * S;
    c.propagationNs = 1000000;
    return c;
}
void Formula()
{
    auto f = Forecast();
    Near(N5cCatchSeconds(f), .3325, "V4 state catch-up equation");
    Near(N5cBudgetSeconds(f, S), 11, "original compute deadline budget");
    f.input.inputPolicy = InputStagingPolicy::DEFERRED;
    Near(N5cCatchSeconds(f), 1.3325, "deferred complete INPUT wait");
    f.inputLocal = true;
    Near(N5cCatchSeconds(f), .3325, "local delivery has exactly zero INPUT wait");
    f.config.batchN = 1;
    Near(N5cCatchSeconds(f), .25, "n=1 has neither tail nor remote merge term");
    f.input.inputPolicy = InputStagingPolicy::EAGER;
    for (uint32_t d = 10; d <= 100; ++d)
        for (uint32_t n = 1; n <= 1000 / d; ++n)
        {
            f.config = {d, n};
            const double expected = 1000.0 * (n - 1) * d / 1000 / 2000 +
                .01 * (n - 1) / n + 1000.0 * d / 1000 / 200;
            Near(N5cCatchSeconds(f), expected, "all fixed legal configurations honor V4");
        }
    f.config = {0, 1}; Reject([&] { N5cCatchSeconds(f); });
}
void Conflict()
{
    auto c = Candidate();
    auto s = ScoreN5cCandidate(c, N5cVariant::FULL);
    Check(s.feasible && !s.noPredictedDemand, "own demand must be effective");
    Near(s.demandProbability, .5, "unconditional first failure mass");
    Near(s.recoveryConflict, 0, "empty peer set has no contention");
    c.peers = {Forecast(2, 2), Forecast(3, 3)};
    c.peers[0].input.risk.futureSteps[0].combinedStepFailureProbability = .6;
    c.peers[1].input.risk.futureSteps[0].combinedStepFailureProbability = .5;
    s = ScoreN5cCandidate(c, N5cVariant::FULL);
    Near(s.recoveryConflict, .8, "independent primaries combine by complement product");
    Near(s.weightedConflict, .4, "own first-failure weight retained in numerator");
    c.demand.readyAfterNs = 2 * S;
    c.demand.input.risk.futureSteps = {{2 * S, .5}, {3 * S, .5}, {4 * S, .5}};
    auto windows = N5cRecoveryWindows(c.demand);
    Check(windows.size() == 2, "same-ns READY excluded");
    Near(windows[0].mass, .25, "pre-ready failure still reduces later survival");
    Near(windows[1].mass, .125, "no pre-ready mass renormalization");
    c.demand.dependenciesAvailable = false;
    Check(N5cRecoveryWindows(c.demand).empty(), "missing INPUT dependency cannot be ready");
    c.demand.dependenciesAvailable = true;
    c.demand.input.risk.futureSteps.clear();
    s = ScoreN5cCandidate(c, N5cVariant::FULL);
    Check(s.noPredictedDemand && s.recoveryConflict == 0, "zero demand explicit fallback");
    c = Candidate(); c.peers = {Forecast(2, 1)};
    Reject([&] { ScoreN5cCandidate(c, N5cVariant::FULL); });
    c = Candidate(); c.peers = {Forecast(2, 2)};
    const auto end = N5cRecoveryWindows(c.peers.front()).front().endNs;
    c.demand.input.work = 3000; c.demand.input.deadlineNs = 50 * S;
    c.demand.input.risk.futureSteps = {{end, .5}};
    Near(ScoreN5cCandidate(c, N5cVariant::FULL).recoveryConflict, 0, "occupancy end is exclusive");
    c.demand.input.risk.futureSteps = {{end - 1, .5}};
    Near(ScoreN5cCandidate(c, N5cVariant::FULL).recoveryConflict, .5, "occupancy includes last ns");
    c.demand.input.risk.futureSteps = {{2 * S, 1.1}};
    Reject([&] { N5cRecoveryWindows(c.demand); });
}
void Ranking()
{
    N5cPlacementPolicy policy;
    auto a = Candidate(10), b = Candidate(11);
    a.recoveryBusyNs = 5 * S;
    auto result = policy.SelectRemote({a, b});
    Check(result.remoteNode == 11 && result.feasibleCount == 2, "history includes actual recovery");
    a = Candidate(10); b = Candidate(11); b.propagationNs = 0;
    result = policy.SelectRemote({a, b});
    Check(result.remoteNode == 11 && result.tieBreak == "PROPAGATION", "pressure ties use path delay");
    b.propagationNs = a.propagationNs;
    result = policy.SelectRemote({b, a});
    Check(result.remoteNode == 10 && result.tieBreak == "STABLE_NODE_ID", "deterministic ID final tie");
    a.additionalQuotaBytes = 1001;
    Check(!ScoreN5cCandidate(a, N5cVariant::NO_M).feasible, "noM retains hard storage capacity");
    a = Candidate(); a.exposureNs = a.normalBusyNs = 0;
    Check(ScoreN5cCandidate(a, N5cVariant::FULL).historyUnavailable, "cold start has diagnostic");
    a.recoveryBusyNs = 1;
    Reject([&] { ScoreN5cCandidate(a, N5cVariant::FULL); });
    a = Candidate(); a.demand.input.deadlineNs = 2 * S;
    Check(!ScoreN5cCandidate(a, N5cVariant::FULL).feasible, "original deadline is a hard constraint");
    a = Candidate(); a.normalBusyNs = 8 * S; a.additionalQuotaBytes = 900;
    Near(ScoreN5cCandidate(a, N5cVariant::FULL).bottleneck, .9, "storage is dominant");
    Near(ScoreN5cCandidate(a, N5cVariant::NO_M).bottleneck, .8, "noM removes only scoring dimension");
    a = Candidate(); a.normalBusyNs = 8 * S;
    Near(ScoreN5cCandidate(a, N5cVariant::NO_U).bottleneck, .1, "noU preserves storage score");
    a.peers = {Forecast(2, 2)};
    Near(ScoreN5cCandidate(a, N5cVariant::NO_R).recoveryConflict, .5, "noR still records raw conflict");
}
void Quotas()
{
    N5cQuotaLedger ledger;
    std::map<uint64_t, uint64_t> actual{{1, 100}, {2, 50}};
    ledger.Replace(1, 10, 200);
    Check(ledger.Peak(1, 10) == 200 && !ledger.Peak(1, 11) && !ledger.Peak(2, 10),
          "maintenance quota lookup changed owner/node scope");
    Check(ledger.Accounted(10, actual) == 250, "actual and its quota are not added twice");
    ledger.Replace(1, 10, 150);
    Check(ledger.Accounted(10, actual) == 200, "ON replaces old quota");
    Check(ledger.Accounted(10, actual, 1) == 150, "replacement keeps actual objects");
    ledger.Replace(1, 10, 40);
    Check(ledger.Accounted(10, actual) == 150, "smaller promise cannot erase actual occupancy");
    Reject([&] { ledger.Replace(1, 11, 40); });
    ledger.Release(1); ledger.Release(1);
    Check(!ledger.Peak(1, 10), "released maintenance quota remains visible");
    Check(ledger.Empty() && ledger.Accounted(10, actual) == 150, "release preserves recovery objects");
    ledger.Replace(1, 10, std::numeric_limits<uint64_t>::max());
    Reject([&] { ledger.Accounted(10, actual); });
    for (const auto name : {"full", "noR", "noU", "noM", "recent-U", "rational-U"})
        Check(std::string(N5cVariantName(ParseN5cVariant(name))) == name, "variant roundtrip");
    Reject([] { ParseN5cVariant("weighted"); });
    Check(CompFrrPlacementPolicy(ComputePressurePolicy::CUMULATIVE).Variant() == N5cVariant::FULL,
          "formal CUMULATIVE must preserve full compatibility entry");
    Check(CompFrrPlacementPolicy(ComputePressurePolicy::IDLE_AWARE).Variant() == N5cVariant::RATIONAL_U,
          "formal IDLE_AWARE must preserve rational-U compatibility entry");
    Check(CompFrrPlacementPolicy(ComputePressurePolicy::CUMULATIVE).SelectRemote({Candidate()}).remoteNode ==
              N5cPlacementPolicy().SelectRemote({Candidate()}).remoteNode,
          "formal naming changed placement decisions");
    Check(CumulativeComputePressure(2, 10) == 0.2 && CumulativeComputePressure(0, 0) == 0 &&
              IdleAwareComputePressure(0.8, 100, 300) == 0.2,
          "formal pressure extraction changed exact arithmetic");
}
void ObservationAndGate()
{
    BackupStoragePool pool(1000);
    int64_t previous = 0;
    uint64_t held = 0, integral = 0, changes = 0, peaks = 0;
    pool.SetPeakObserver([&] { ++peaks; });
    pool.SetChangeObserver([&] {
        const auto now = Simulator::Now().GetNanoSeconds();
        integral += (now - previous) * held;
        previous = now; held = pool.Used() + pool.Reserved(); ++changes;
    });
    uint64_t state{}, batch{};
    Simulator::Schedule(NanoSeconds(10), [&] { state = *pool.Allocate(1, StorageKind::REMOTE_STATE, 100); });
    Simulator::Schedule(NanoSeconds(20), [&] { batch = *pool.TryReserve(1, StorageKind::REMOTE_BATCH, 200); });
    Simulator::Schedule(NanoSeconds(30), [&] { Check(pool.CommitReservation(batch), "commit observation"); });
    Simulator::Schedule(NanoSeconds(40), [&] { Check(pool.Merge(state, batch, 250), "merge observation"); });
    Simulator::Schedule(NanoSeconds(50), [&] {
        Check(pool.OccupancyByTask().at(1) == 250, "merge inventory snapshot exact");
        Check(pool.ReleaseTask(1) == 1, "terminal release observation");
    });
    Simulator::Run();
    Check(integral == 9500 && changes == 5 && peaks == 3 && held == 0,
          "physical byte-time observer missed merge/release or replaced old peak callback");
    Simulator::Destroy();
    FrequencyDecisionGate gate;
    FrequencyDecision proposal;
    proposal.phase = ProtectionPhase::OFF; proposal.epochNs = 1;
    proposal.action = FrequencyAction::START; proposal.selected = FrequencyCandidate{};
    proposal.selected->config = {50,4};
    gate.Propose(proposal);
    Check(!gate.Resolve(1, false, true, false) && gate.Phase() == ProtectionPhase::OFF,
          "post-batch OFF resource race must not initialize");
    proposal.epochNs = 2; gate.Propose(proposal);
    Check(gate.Resolve(2, false, true), "valid START gate"); gate.InitializationCommitted();
    proposal.phase = ProtectionPhase::ON; proposal.action = FrequencyAction::UPDATE;
    proposal.epochNs = 3; proposal.selected->config = {30,5}; gate.Propose(proposal);
    Check(gate.Resolve(3, false, true, false) && gate.Paused() &&
          *gate.CurrentConfig() == FrequencyConfiguration{50,4},
          "post-batch ON race must pause and retain committed configuration");
    proposal.epochNs = 4; gate.Propose(proposal);
    Check(gate.Resolve(4, false, true) && !gate.Paused() &&
          *gate.CurrentConfig() == FrequencyConfiguration{30,5}, "later legal update must resume");
}
void RecoveryObservation()
{
    struct Callbacks
    {
        void Task(uint64_t, uint32_t, int64_t) {}
        void Recovery(uint64_t, uint64_t, uint32_t, int64_t) {}
        Ptr<ComputeService> service;
        ComputeUsageHistory history;
        void State(uint32_t node, bool busy)
        {
            using A = ComputeUsageHistory::Activity;
            history.Observe(node, Simulator::Now().GetNanoSeconds(),
                !busy ? A::IDLE : (service->HasRecoveryReservation() ? A::RECOVERY : A::NORMAL));
        }
    } callbacks;
    auto node = CreateObject<Node>();
    auto service = CreateObject<ComputeService>();
    callbacks.service = service;
    callbacks.history.Observe(1, 0, ComputeUsageHistory::Activity::IDLE);
    service->ConnectStateObserver(MakeCallback(&Callbacks::State, &callbacks));
    service->Configure(1, 1000000000, MakeCallback(&Callbacks::Task, &callbacks),
                       MakeCallback(&Callbacks::Task, &callbacks));
    node->AddApplication(service);
    service->SetStartTime(NanoSeconds(0)); service->SetStopTime(NanoSeconds(100));
    Simulator::Schedule(NanoSeconds(10), [&] { Check(service->ReserveRecovery(1,1), "reserve recovery"); });
    Simulator::Schedule(NanoSeconds(20), [&] {
        Check(service->GetRecoveryBusyTimeNs() == 0, "reserved idle is not actual CPU use");
        Check(callbacks.history.Query(1, 10, 20, 20, 20).recoveryNs == 0,
              "recent history counted a reservation as execution");
        const auto cb = MakeCallback(&Callbacks::Recovery, &callbacks);
        Check(service->StartRecovery(1,1,50,10,cb,cb,cb), "start recovery");
    });
    Simulator::Schedule(NanoSeconds(30), [&] { service->SetComputeAvailable(false); });
    Simulator::Schedule(NanoSeconds(50), [&] {
        Check(service->GetRecoveryBusyTimeNs() == 30 && service->GetBusyTimeNs() == 30,
              "live immune execution omitted or counted outside total busy prefix");
        Check(callbacks.history.Query(1, 25, 50, 50, 50).recoveryNs == 25,
              "recent window lost live immune recovery after temporary compute failure");
        Check(service->CancelRecovery(1,1), "cancel recovery");
    });
    Simulator::Stop(NanoSeconds(60)); Simulator::Run();
    Check(service->GetRecoveryBusyTimeNs() == 30 && service->GetBusyTimeNs() == 30,
          "interrupted recovery used planned rather than actual CPU duration");
    const auto recent = callbacks.history.Query(1, 40, 60, 60, 60);
    Check(recent.recoveryNs == 10 && recent.normalNs == 0 && recent.exposureNs == 20,
          "cancelled recovery did not close recent actual-service interval");
    Check(callbacks.history.IdleTimeNs(1, 19) == 19 && callbacks.history.IdleTimeNs(1, 40) == 0 &&
          callbacks.history.IdleTimeNs(1, 60) == 10,
          "reservation/immunity/cancel changed actual continuous-idle semantics");
    service->DisconnectStateObserver(MakeCallback(&Callbacks::State, &callbacks));
    Simulator::Destroy();
}
void RecentHistory()
{
    using A = ComputeUsageHistory::Activity;
    ComputeUsageHistory history;
    history.Observe(10, 0, A::IDLE);
    history.Observe(10, 10, A::NORMAL);
    history.Observe(10, 30, A::IDLE);
    history.Observe(10, 50, A::RECOVERY);
    history.Observe(10, 70, A::IDLE);
    history.Observe(10, 70, A::NORMAL);
    history.Observe(10, 80, A::IDLE);
    // Exhaustively compare prefix queries with an independent per-ns ground truth.
    for (int64_t end = 0; end <= 100; ++end)
        for (int64_t begin = 0; begin <= end; ++begin)
        {
            uint64_t normal = 0, recovery = 0;
            for (auto t = begin; t < end; ++t)
            {
                normal += (t >= 10 && t < 30) || (t >= 70 && t < 80);
                recovery += t >= 50 && t < 70;
            }
            const auto w = history.Query(10, begin, end, end, end);
            Check(w.normalNs == normal && w.recoveryNs == recovery &&
                  w.exposureNs == static_cast<uint64_t>(end - begin),
                  "past-only prefix/window or same-ns boundary mismatch");
        }
    const auto dead = history.Query(10, 60, 100, 100, 80);
    Check(dead.normalNs == 10 && dead.recoveryNs == 10 && dead.exposureNs == 20,
          "F3 survival endpoint not clipped consistently");
    Check(history.Query(10, 90, 100, 100, 80).exposureNs == 0, "post-F3 has no exposure");
    Reject([&] { history.Query(10, 0, 101, 100, 100); });
    Reject([&] { history.Query(10, -1, 10, 10, 10); });
    Reject([&] { history.Query(11, 0, 10, 10, 10); });
    Reject([&] { history.Query(10, 0, 80, 80, 20); });
    history.Observe(10, 100, A::IDLE);
    Reject([&] { history.Observe(10, 99, A::IDLE); });
    auto oldBusy = Candidate(10), recentBusy = Candidate(11);
    oldBusy.normalBusyNs = 8 * S;
    oldBusy.recentExposureNs = recentBusy.recentExposureNs = S;
    recentBusy.recentNormalBusyNs = S / 2;
    Check(N5cPlacementPolicy().SelectRemote({oldBusy, recentBusy}).remoteNode == 11,
          "FULL cumulative ranking changed");
    Check(N5cPlacementPolicy(N5cVariant::RECENT_U).SelectRemote({oldBusy, recentBusy}).remoteNode == 10,
          "recent-U did not distinguish old load from recent load");
    auto score = ScoreN5cCandidate(oldBusy, N5cVariant::RECENT_U);
    Near(score.historicalUtilization, .8, "recent-U overwrote cumulative diagnostic");
    Near(score.recentUtilization, 0, "recent idle candidate has nonzero recent U");
    oldBusy.peers = {Forecast(2, 2)};
    Near(ScoreN5cCandidate(oldBusy, N5cVariant::RECENT_U).bottleneck, .5,
         "recent-U removed the R dimension");
    oldBusy.additionalQuotaBytes = 1001;
    Check(!ScoreN5cCandidate(oldBusy, N5cVariant::RECENT_U).feasible,
          "recent-U bypassed hard storage feasibility");
    oldBusy = Candidate(10);
    Check(ScoreN5cCandidate(oldBusy, N5cVariant::RECENT_U).historyUnavailable,
          "zero-length window needs explicit unavailable diagnostic");
}
void RationalHistory()
{
    using A = ComputeUsageHistory::Activity;
    ComputeUsageHistory h;
    h.Observe(1, 0, A::IDLE);
    Check(h.IdleTimeNs(1, 15) == 15, "never busy should be idle from birth");
    h.Observe(1, 10, A::NORMAL);
    Check(h.IdleTimeNs(1, 9) == 9 && h.IdleTimeNs(1, 10) == 0, "idle read future activity");
    h.Observe(1, 20, A::IDLE);
    h.Observe(1, 30, A::IDLE); // Temporary unavailability / repeated notification is not busy.
    Check(h.IdleTimeNs(1, 35) == 15, "nonservice notification reset idle");
    h.Observe(1, 40, A::RECOVERY);
    h.Observe(1, 50, A::IDLE);
    h.Observe(1, 50, A::NORMAL); // Back-to-back service gives no idle gap.
    h.Observe(1, 60, A::IDLE);
    h.Observe(1, 70, A::NORMAL);
    h.Observe(1, 70, A::IDLE); // Zero-duration start/cancel does not create actual busy time.
    Check(h.IdleTimeNs(1, 49) == 0 && h.IdleTimeNs(1, 50) == 0 &&
          h.IdleTimeNs(1, 65) == 5 && h.IdleTimeNs(1, 80) == 20,
          "recovery/same-ns boundary corrupted idle clock");
    Reject([&] { h.IdleTimeNs(2, 0); });
    Reject([&] { h.IdleTimeNs(1, -1); });
    Near(N5cRationalPressure(.6, 10, 0), .6, "I=0");
    Near(N5cRationalPressure(.6, 10, 10), .3, "I=H");
    Near(N5cRationalPressure(.6, 10, 90), .06, "idle decay");
    Near(N5cRationalPressure(0, 10, 90), 0, "never busy pressure");
    Near(N5cRationalPressure(.6, INT64_MAX, INT64_MAX), .3, "integer horizon sum overflow");
    Reject([] { N5cRationalPressure(.6, 0, 0); });
    Reject([] { N5cRationalPressure(.6, 1, -1); });
    Reject([] { N5cRationalPressure(1.1, 1, 0); });
    double previous = 1;
    for (int64_t i = 0; i < 1000; ++i)
    {
        const double p = N5cRationalPressure(.6, 10, i);
        Check(p > 0 && p <= .6 && p <= previous, "rational range/monotonicity/positive history");
        previous = p;
    }
    auto stale = Candidate(10), fresh = Candidate(11);
    stale.historyHorizonNs = fresh.historyHorizonNs = S;
    stale.normalBusyNs = 8 * S; stale.continuousIdleNs = 9 * S;
    fresh.normalBusyNs = 2 * S;
    Check(N5cPlacementPolicy().SelectRemote({stale, fresh}).remoteNode == 11, "FULL redefined");
    auto policy = N5cPlacementPolicy(N5cVariant::RATIONAL_U);
    Check(policy.SelectRemote({stale, fresh}).remoteNode == 10, "rational ignored stale history");
    auto score = ScoreN5cCandidate(stale, N5cVariant::RATIONAL_U);
    Near(score.historicalUtilization, .8, "rational overwrote global utilization");
    Near(score.rationalPressure, .08, "rational pressure");
    stale.peers = {Forecast(2, 2)};
    Near(ScoreN5cCandidate(stale, N5cVariant::RATIONAL_U).bottleneck, .5, "rational removed R");
    stale.additionalQuotaBytes = 1001;
    Check(!ScoreN5cCandidate(stale, N5cVariant::RATIONAL_U).feasible, "rational bypassed storage");
    stale.rejection = "NODE_UNAVAILABLE";
    Check(policy.SelectRemote({stale, fresh}).remoteNode == 11, "zero idle admitted unavailable node");
    stale = Candidate(10); stale.historyHorizonNs = S; stale.exposureNs = stale.normalBusyNs = 0;
    Check(ScoreN5cCandidate(stale, N5cVariant::RATIONAL_U).historyUnavailable,
          "missing history needs explicit diagnostic");
    fresh = stale; fresh.remoteNode = 11; stale.propagationNs = 2;
    fresh.propagationNs = 1;
    Check(policy.SelectRemote({stale, fresh}).remoteNode == 11, "propagation tie-break changed");
    stale.propagationNs = 1;
    Check(policy.SelectRemote({fresh, stale}).remoteNode == 10, "stable ID tie-break changed");
}
} // namespace
int main()
{
    try
    {
        Formula(); Conflict(); Ranking(); Quotas(); ObservationAndGate(); RecoveryObservation(); RecentHistory(); RationalHistory();
        std::cout << "N5C V4: " << checks << " invariant checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "N5C V4 FAILED: " << e.what() << '\n';
        return 1;
    }
}
