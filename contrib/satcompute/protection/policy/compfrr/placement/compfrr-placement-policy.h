/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_PLACEMENT_POLICY_H
#define SATCOMPUTE_COMPFRR_PLACEMENT_POLICY_H
#include "../../placement/fa-first-feasible/fa-first-feasible-placement-policy.h"
#include "../../../common/protection-forecast.h"
#include "../../../common/placement-resources.h"
#include "compute-pressure/compute-pressure.h"
#include <map>
#include <optional>

namespace ns3::protection
{
/** Only the ranking dimensions change in ablations; all hard constraints remain. */
enum class N5cVariant { FULL, NO_R, NO_U, NO_M, RECENT_U, RATIONAL_U };
N5cVariant ParseN5cVariant(const std::string& name);
const char* N5cVariantName(N5cVariant variant);

/** Causal per-task demand on one remote, never an observed recovery or future trace. */
struct CompFrrForecast
{
    uint64_t taskId{};
    uint32_t primaryNode{};
    PlacementForecastInput input; ///< Shared model state, costs and original deadline.
    CheckpointCadence config; ///< Already solved; no Frequency call during ranking.
    int64_t readyAfterNs{}; ///< Strict readiness cutoff, actual or explicitly estimated.
    bool dependenciesAvailable{true};
    bool inputLocal{}; ///< LocalDelivery contributes exactly zero INPUT network time.
    /** Candidate-specific hard-admission term. Empty preserves the legacy layout term. */
    std::optional<double> recoveryInputSeconds;
};

/** First-failure mass and hypothetical resource occupancy [start,end). */
struct CompFrrOccupancyWindow
{
    int64_t startNs{}, endNs{};
    long double mass{};
};

/** Raw resource and forecast snapshot for one remote candidate. */
struct CompFrrCandidate : PlacementResourceSnapshot
{
    CompFrrForecast demand;
    std::vector<CompFrrForecast> peers;
    int64_t propagationNs{};
    std::string rejection; ///< Node/path/local-storage checks performed by causal adapter.
};

/** Auditable values including infeasible candidates, without changing their data. */
struct CompFrrScore
{
    uint32_t remoteNode{};
    bool feasible{}, historyUnavailable{}, noPredictedDemand{};
    double recoveryConflict{}, historicalUtilization{}, storagePressure{}, bottleneck{};
    double recentUtilization{}; ///< Used only by RECENT_U; cumulative U keeps its original meaning.
    double rationalPressure{}; ///< RATIONAL_U only; not a replacement for measured utilization.
    double demandProbability{}, weightedConflict{}, catchSeconds{}, budgetSeconds{};
    int64_t propagationNs{};
    uint64_t peerCount{}, windowCount{};
    std::string reason, dominant;
};

/** Selection is a proposal only; the shared runtime performs real admission later. */
struct CompFrrSelection
{
    std::vector<CompFrrScore> scores;
    std::optional<uint32_t> remoteNode;
    uint64_t feasibleCount{};
    std::string tieBreak{"NONE"};
};

/** V4 formulas only: no optimizer, event scheduling, fault draws or workload prediction. */
double CompFrrCatchSeconds(const CompFrrForecast& forecast);
double CompFrrBudgetSeconds(const CompFrrForecast& forecast, int64_t atNs);
std::vector<CompFrrOccupancyWindow> CompFrrRecoveryWindows(const CompFrrForecast& forecast);
CompFrrScore ScoreCompFrrCandidate(const CompFrrCandidate& candidate, N5cVariant variant);
/** FA-FFP ranks the read-only reference pair; V4 alone ranks the actual remote. */
class CompFrrPlacementPolicy : public FaFirstFeasiblePlacementPolicy
{
  public:
    explicit CompFrrPlacementPolicy(N5cVariant variant = N5cVariant::FULL) : m_variant(variant) {}
    /** Formal policies have exactly two values. Legacy variants below preserve ablation/fixture APIs. */
    explicit CompFrrPlacementPolicy(ComputePressurePolicy pressure)
        : m_variant(pressure == ComputePressurePolicy::IDLE_AWARE ? N5cVariant::RATIONAL_U : N5cVariant::FULL) {}
    const char* Name() const override { return "n5c"; }
    N5cVariant Variant() const { return m_variant; }
    CompFrrSelection SelectRemote(const std::vector<CompFrrCandidate>& candidates) const;
    void RankBackupNodes(std::vector<uint32_t>&, const PlacementContext&) const override;
  private:
    N5cVariant m_variant; ///< Preselected ablation, never tuned from outcomes.
};
} // namespace ns3::protection
#endif
