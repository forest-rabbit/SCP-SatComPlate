/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_FREQUENCY_POLICY_H
#define SATCOMPUTE_COMPFRR_FREQUENCY_POLICY_H
#include "../../../../fault/model/compute-failure-predictor.h"
#include "../../../common/protection-types.h"
#include "../../../common/task-state-adapter.h"

#include <functional>
#include <string>

namespace ns3::protection
{
/** One causal future sampler probability, not a sampled event or new decision variable. */
struct FrequencyRiskStep
{
    int64_t targetTimeNs{}; ///< Absolute canonical fault-check time.
    double combinedStepFailureProbability{}; ///< Conditional F1/F2 union.
};

/** Same-epoch causal probabilities. Contains no trace, sample outcome or future fault ID. */
struct FrequencyRisk
{
    int64_t epochNs{};          ///< Current fault-check time, not a task-relative timer.
    int64_t intervalNs{};       ///< Current sample's reference interval and ON cost window.
    double qCurrentSample{};    ///< Union of the unchanged independent F1/F2 samples.
    double pFailBeforeFinish{}; ///< Canonical predictor, including the current check.
    std::vector<FrequencyRiskStep> futureSteps; ///< Read-only canonical trajectory for START.
};

/** Call the existing production predictor and reject a mismatched sampler probability.
 * The adapter supplies the exact q already computed by the current fault epoch.
 * No random value is consumed and no replacement fault model is implemented here.
 */
FrequencyRisk MakeFrequencyRisk(double currentSamplerQ,
                                const ComputeFailurePredictionInput& predictionInput);

/** Immutable frequency only; placement is supplied separately by PlacementPolicy. */
struct FrequencyConfiguration
{
    uint32_t deltaPermille{}; ///< 10..100, step one (0.1 percentage point).
    uint32_t batchN{};        ///< 1..100, n*deltaPermille<=1000.
    bool operator==(const FrequencyConfiguration&) const = default;
};

/** Additional allocation peaks, compared with free bytes, not total pool capacity.
 * The causal adapter must include legal state/H, init, pending and merge allocations,
 * without charging already occupied/reserved bytes twice. Nullopt means infeasible.
 */
struct FrequencyStorageDemand
{
    uint64_t localAdditionalBytes{};  ///< Predicted additional local peak.
    uint64_t remoteAdditionalBytes{}; ///< Predicted additional init/merge peak.
};

using FrequencyStorageEstimator =
    std::function<std::optional<FrequencyStorageDemand>(FrequencyConfiguration)>;

/** Scalar snapshot for independent analytical scoring; no simulator or RNG access. */
struct FrequencyInput
{
    InputStagingPolicy inputPolicy{InputStagingPolicy::EAGER}; ///< Explicit INPUT timing contract.
    ProtectionPhase phase{ProtectionPhase::OFF}; ///< Only OFF and ON make risk decisions.
    FrequencyRisk risk;                          ///< Supplied at the same current fault epoch.
    double inputBytes{};                         ///< Original serialized INPUT S, not Kvar.
    double work{};                               ///< Total task WU.
    double variableBytes{};       ///< Full Kvar, including variable index but not fixed H.
    double progress{};            ///< Actual completed WU divided by W.
    double primaryRate{};         ///< Current primary WU/s.
    double recoveryRate{};        ///< Selected FFP recovery candidate WU/s.
    double inputBandwidth{};      ///< Causal INPUT replay bandwidth, bytes/s.
    double backupBandwidth{};     ///< Causal backup/tail bandwidth, bytes/s.
    double remainingSeconds{};    ///< Actual scheduled remaining primary computation.
    int64_t deadlineNs{};         ///< Original logical deadline, never reset on recovery.
    ProtectionCosts costs{};      ///< Supplied by GetProtectionCosts(Kvar), not another tier table.
    double baseTransferSeconds{}; ///< Causal initialization base transfer estimate.
    double stateTransferSeconds{};           ///< Causal initialization state transfer estimate.
    bool nodeAvailable{};                    ///< FFP candidate currently eligible.
    bool pathAvailable{};                    ///< Required current paths exist.
    bool replayAvailable{true}; ///< Soft for eager OFF; mandatory for deferred protection.
    uint64_t localFreeBytes{};               ///< Actual N5A pool free bytes at decision time.
    uint64_t remoteFreeBytes{};              ///< Actual N5A pool free bytes at decision time.
    FrequencyStorageEstimator storageDemand; ///< Required pure causal per-candidate estimator.
};

/** One candidate's seconds-equivalent score, never a claim about actual runtime latency. */
struct FrequencyCandidate
{
    FrequencyConfiguration config;
    double objective{};              ///< START excludes Cinit here; ON is one epoch's score.
    double averageRecoverySeconds{}; ///< Analytical Rbar.
    double normalSeconds{};          ///< Remaining maintenance or this epoch's maintenance.
    FrequencyStorageDemand storage;  ///< Additional peak demands used for admission.
};

/** Exact deterministic lexicographic comparison, with no epsilon objective tie. */
bool FrequencyCandidateLess(const FrequencyCandidate& a, const FrequencyCandidate& b);

enum class FrequencyAction
{
    NONE,   ///< Not a decision phase or OFF without a beneficial feasible START.
    START,  ///< Proposed initialization, not effective before surviving this epoch.
    UPDATE, ///< Proposed future frequency only.
    PAUSE   ///< Stay ON, retain state, pause new targets AND new batches.
};

/** Pure decision proposal. Fault outcome and commit status belong to the later gate. */
struct FrequencyDecision
{
    FrequencyAction action{FrequencyAction::NONE};
    ProtectionPhase phase{ProtectionPhase::OFF};
    int64_t epochNs{};
    std::string reason;
    double deadlineSlackSeconds{};
    double initializationSeconds{};
    std::optional<double> jOff;
    std::optional<double> jStart;
    std::optional<int64_t> initReadyTimeNs; ///< Estimated ready time, not actual commit evidence.
    std::optional<double> pFailAfterInitReady; ///< Unconditional first-fault mass after ready.
    std::optional<double> representativeProgressAfterReady; ///< Conditional weighted progress.
    std::optional<double> legacyCurrentProgressLoss; ///< Old OFF score, diagnostic only.
    std::optional<FrequencyCandidate> selected;
    uint64_t feasibleCount{};
    uint64_t deadlineRejected{};
    uint64_t storageRejected{};
};

/** CompFRR whether/how-often policy, independent from the validation implementation. */
class CompFrrFrequencyPolicy
{
  public:
    /** Enumerate every legal candidate, then return an uncommitted decision. */
    FrequencyDecision Evaluate(const FrequencyInput& input) const;
};
} // namespace ns3::protection
#endif
