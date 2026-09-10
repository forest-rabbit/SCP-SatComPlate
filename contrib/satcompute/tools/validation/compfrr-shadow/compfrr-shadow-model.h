/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_SHADOW_MODEL_H
#define SATCOMPUTE_COMPFRR_SHADOW_MODEL_H

#include "ns3/compute-task.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace ns3::compfrr
{
/** G1 variable-state budget and legal completed-WU boundaries; no fixed H. */
struct WorkloadLayout
{
    uint64_t work{};                  ///< Total WU.
    uint64_t variableBytes{};         ///< Full variable state, including its variable index.
    uint64_t extent{};                ///< Application extent: image bytes or complete tokens.
    bool tokens{};                    ///< Token boundaries require whole-token state.
    std::vector<uint64_t> boundaries; ///< Unique increasing legal WU endpoints, including zero.
    std::vector<uint64_t> applicationEnds; ///< Last application endpoint for each deduplicated WU.
    /** @return State available at a legal completed-work boundary. */
    uint64_t StateAt(uint64_t completedWork) const;
    /** @return Greatest already completed legal boundary. */
    uint64_t Floor(uint64_t completedWork) const;
    /** @return Next legal boundary at/after target, strictly after previous, or none. */
    std::optional<uint64_t> Next(uint64_t targetWork, uint64_t previousWork) const;
    /** G1 rounds nominal progress to application extent first, then to legal WU. */
    std::optional<uint64_t> NextProgress(uint64_t baseWork,
                                         int deltaPermille,
                                         uint64_t previousWork) const;
};

/** Port of G1 task_workload_model and preview_unit_ends; rejects mismatched WU. */
WorkloadLayout MakeWorkloadLayout(const TaskDefinition& task);

/** Fixed cost tier selected using full K variable, with decimal MB boundaries. */
struct Costs
{
    double local{};  ///< c_L in seconds.
    double remote{}; ///< c_R in seconds.
    int tier{};      ///< 1, 2, or 3.
};

/** @return G4's immutable full-state cost tier. */
Costs CostTier(uint64_t variableBytes);

/** Current causal inputs to pure decision formulas. */
struct DecisionInput
{
    double inputBytes{};       ///< S, not K.
    double work{};             ///< Total WU.
    double variableBytes{};    ///< K variable.
    double rate{};             ///< Actual compute profile WU/s.
    double bandwidth{};        ///< Link bit/s divided by eight.
    double progress{};         ///< Actual completed WU / W.
    double remainingSeconds{}; ///< Actual scheduled remaining compute time.
    double deadlineSlack{};    ///< Deadline - now - remaining compute time; may be negative.
    double qOneSecond{};       ///< Read-only next-one-second F1/F2 union risk.
    double pFinish{};          ///< Read-only risk through compute completion.
    Costs costs;               ///< Tier selected once per task.
    bool nodeAvailable{true};  ///< G4 nonbinding candidate resource assumption.
    bool pathAvailable{true};  ///< G4 nonbinding candidate resource assumption.
};

/** One legal frequency choice. */
struct Candidate
{
    int deltaPermille{};     ///< 10..100 inclusive, step 1.
    int remoteEvery{};       ///< n, with n * deltaPermille <= 1000.
    double objective{};      ///< J, excluding C_init for a START candidate.
    double averageCatchUp{}; ///< Analytical R bar.
};

/** Deterministic complete enumeration, exact ties resolved by (J, delta, n). */
struct Selection
{
    std::optional<Candidate> best; ///< Empty when no candidate is feasible.
    uint64_t feasibleCount{};      ///< Number admitted by all constraints.
};

/** @return Best START or ON candidate; ON decision interval is one second. */
Selection SelectFrequency(const DecisionInput& input, bool alreadyOn);
/** @return Recompute OFF catch-up seconds, excluding future useful computation. */
double RecomputeCatchUp(const DecisionInput& input);
/** @return Initialization duration using the actual legal state at START. */
double InitializationTime(const DecisionInput& input, uint64_t legalStateBytes);
/** Strict START comparison plus initialization feasibility; a tie remains OFF. */
bool ShouldStartProtection(const DecisionInput& input,
                           const Selection& selection,
                           double initializationSeconds);

/** Separate executed/idle accounting prevents double counting recompute time. */
struct CatchUp
{
    double seconds{};         ///< Analytical catch-up time after an observed fault.
    double executedWasteWu{}; ///< Redo WU plus tail merge WU, not useful future work.
    double idleWasteWu{};     ///< Transmission waiting times compute rate.
};

/** @return OFF/ON observed-fault cost; no probability multiplier. */
CatchUp EstimateCatchUp(const DecisionInput& input,
                        bool protectedOn,
                        double localProgress,
                        double remoteProgress);
} // namespace ns3::compfrr
#endif
