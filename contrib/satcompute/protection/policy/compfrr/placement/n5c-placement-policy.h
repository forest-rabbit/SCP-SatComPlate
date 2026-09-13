/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_N5C_PLACEMENT_POLICY_H
#define SATCOMPUTE_N5C_PLACEMENT_POLICY_H
#include "../../baseline/fa-first-feasible-placement/fa-first-feasible-placement-policy.h"
#include "../frequency/compfrr-frequency-policy.h"
#include <map>

namespace ns3::protection
{
/** Only the ranking dimensions change in ablations; all hard constraints remain. */
enum class N5cVariant { FULL, NO_R, NO_U, NO_M, RECENT_U };
N5cVariant ParseN5cVariant(const std::string& name);
const char* N5cVariantName(N5cVariant variant);

/** Causal per-task demand on one remote, never an observed recovery or future trace. */
struct N5cForecast
{
    uint64_t taskId{};
    uint32_t primaryNode{};
    FrequencyInput input; ///< Shared model state, costs and original deadline.
    FrequencyConfiguration config; ///< Already solved; no Frequency call during ranking.
    int64_t readyAfterNs{}; ///< Strict readiness cutoff, actual or explicitly estimated.
    bool dependenciesAvailable{true};
    bool inputLocal{}; ///< LocalDelivery contributes exactly zero INPUT network time.
};

/** First-failure mass and hypothetical resource occupancy [start,end). */
struct N5cOccupancyWindow
{
    int64_t startNs{}, endNs{};
    long double mass{};
};

/** Raw resource and forecast snapshot for one remote candidate. */
struct N5cCandidate
{
    uint32_t remoteNode{};
    N5cForecast demand;
    std::vector<N5cForecast> peers;
    uint64_t normalBusyNs{}, recoveryBusyNs{}, exposureNs{};
    int64_t historyHorizonNs{}, historyWindowBeginNs{}, historyWindowEndNs{}; ///< Exact primary horizon.
    uint64_t recentNormalBusyNs{}, recentRecoveryBusyNs{}, recentExposureNs{}; ///< Past-only window.
    uint64_t capacityBytes{}, accountedBytes{}, additionalQuotaBytes{};
    int64_t propagationNs{};
    std::string rejection; ///< Node/path/local-storage checks performed by causal adapter.
};

/** Auditable values including infeasible candidates, without changing their data. */
struct N5cScore
{
    uint32_t remoteNode{};
    bool feasible{}, historyUnavailable{}, noPredictedDemand{};
    double recoveryConflict{}, historicalUtilization{}, storagePressure{}, bottleneck{};
    double recentUtilization{}; ///< Used only by RECENT_U; cumulative U keeps its original meaning.
    double demandProbability{}, weightedConflict{}, catchSeconds{}, budgetSeconds{};
    int64_t propagationNs{};
    uint64_t peerCount{}, windowCount{};
    std::string reason, dominant;
};

/** Selection is a proposal only; the shared runtime performs real admission later. */
struct N5cSelection
{
    std::vector<N5cScore> scores;
    std::optional<uint32_t> remoteNode;
    uint64_t feasibleCount{};
    std::string tieBreak{"NONE"};
};

/** V4 formulas only: no optimizer, event scheduling, fault draws or workload prediction. */
double N5cCatchSeconds(const N5cForecast& forecast);
double N5cBudgetSeconds(const N5cForecast& forecast, int64_t atNs);
std::vector<N5cOccupancyWindow> N5cRecoveryWindows(const N5cForecast& forecast);
N5cScore ScoreN5cCandidate(const N5cCandidate& candidate, N5cVariant variant);

/** Per-task remote peak promises; actual objects are never counted a second time. */
class N5cQuotaLedger
{
  public:
    void Replace(uint64_t task, uint32_t node, uint64_t peakBytes);
    void Release(uint64_t task);
    /** Sum max(actual,quota), optionally omitting the replaced task's old quota only. */
    uint64_t Accounted(uint32_t node, const std::map<uint64_t, uint64_t>& actual,
                       std::optional<uint64_t> replacing = {}) const;
    bool Empty() const { return m_quotas.empty(); }
  private:
    std::map<uint64_t, std::pair<uint32_t, uint64_t>> m_quotas; ///< Task -> fixed remote/peak.
};

/** FA-FFP ranks the read-only reference pair; V4 alone ranks the actual remote. */
class N5cPlacementPolicy : public FaFirstFeasiblePlacementPolicy
{
  public:
    explicit N5cPlacementPolicy(N5cVariant variant = N5cVariant::FULL) : m_variant(variant) {}
    const char* Name() const override { return "n5c"; }
    N5cVariant Variant() const { return m_variant; }
    N5cSelection SelectRemote(const std::vector<N5cCandidate>& candidates) const;
    void RankBackupNodes(std::vector<uint32_t>&, const PlacementContext&) const override;
  private:
    N5cVariant m_variant; ///< Preselected ablation, never tuned from outcomes.
};
} // namespace ns3::protection
#endif
