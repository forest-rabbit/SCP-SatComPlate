/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SELF_STATE_FAULT_MODEL_H
#define SATCOMPUTE_SELF_STATE_FAULT_MODEL_H

#include "fault-model-config.h"

namespace ns3
{

/** Complete F1 state for one compute node at one model boundary. */
struct SelfStateFaultSnapshot
{
    double temperatureC{}; ///< Current thermal proxy in degrees Celsius.
    double depthOfDischarge{}; ///< Current normalized battery depth of discharge.
    double thermalRisk{}; ///< Normalized temperature risk in [0, 1].
    double energyPressure{}; ///< Normalized energy pressure in [0, 1].
    double combinedRisk{}; ///< Combined F1 risk in [0, 1].
    double failureIntensityPerSecond{}; ///< Current exponential hazard intensity.
    double stepFailureProbability{}; ///< Failure probability for the latest interval.
    bool busy{}; ///< Whether the latest positive interval used the heating branch.
};

/** Pure F1 thermal, energy, risk, and hazard calculations for one compute node. */
class SelfStateFaultModel
{
  public:
    /** Construct a pure model from a strictly validated F1 configuration. */
    explicit SelfStateFaultModel(const SelfStateFaultConfig& config);

    /** @return Baseline temperature and configured initial depth of discharge. */
    SelfStateFaultSnapshot CreateInitialSnapshot() const;

    /**
     * Advance one node by an exact closed-form interval.
     *
     * @param snapshot Mutable node state.
     * @param busy True for heating and compute-energy consumption.
     * @param intervalSeconds Non-negative interval length in seconds.
     */
    void Update(SelfStateFaultSnapshot& snapshot,
                bool busy,
                double intervalSeconds) const;

    /**
     * Test the configured F1 notice threshold.
     *
     * @param snapshot Current node state.
     * @return True when combined risk is at least the configured threshold.
     */
    bool IsRiskActive(const SelfStateFaultSnapshot& snapshot) const;

  private:
    SelfStateFaultConfig m_config; ///< Immutable validated F1 parameters.
};

} // namespace ns3

#endif // SATCOMPUTE_SELF_STATE_FAULT_MODEL_H
