/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_COMPUTE_FAULT_COMBINATION_H
#define SATCOMPUTE_COMPUTE_FAULT_COMBINATION_H

namespace ns3
{

/** Result of independently sampling the F1 and F2 compute-fault sources. */
struct ComputeFaultSourceOutcome
{
    double combinedProbability{}; ///< Probability that at least one source occurs.
    bool f1Occurred{}; ///< Whether the independent F1 sample occurred.
    bool f2Occurred{}; ///< Whether the independent F2 sample occurred.
    bool computeFaultOccurred{}; ///< Logical OR of the two source outcomes.
};

/**
 * Combine two conditionally independent current-step failure probabilities.
 *
 * @param f1Probability F1 probability in [0, 1].
 * @param f2Probability F2 probability in [0, 1].
 * @return Probability that at least one source occurs.
 */
double CombineComputeFaultProbabilities(double f1Probability,
                                        double f2Probability);

/**
 * Evaluate independent F1 and F2 samples while retaining one platform outcome.
 *
 * Random values are supplied by source-specific ns-3 streams owned by the
 * runtime engine. This pure function never consumes or mutates random state.
 *
 * @param f1Probability F1 probability in [0, 1].
 * @param f1RandomValue F1 uniform sample in [0, 1).
 * @param f2Probability F2 probability in [0, 1].
 * @param f2RandomValue F2 uniform sample in [0, 1).
 * @return Source outcomes, their OR, and the combined probability.
 */
ComputeFaultSourceOutcome EvaluateComputeFaultSources(double f1Probability,
                                                      double f1RandomValue,
                                                      double f2Probability,
                                                      double f2RandomValue);

} // namespace ns3

#endif // SATCOMPUTE_COMPUTE_FAULT_COMBINATION_H
