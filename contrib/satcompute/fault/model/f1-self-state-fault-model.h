/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_F1_SELF_STATE_FAULT_MODEL_H
#define SATCOMPUTE_F1_SELF_STATE_FAULT_MODEL_H

#include "ns3/fault-para.h"
#include <cstdint>

namespace ns3
{

/** Complete F1 state for one compute node at one model boundary. */
struct F1SelfStateFaultSnapshot
{
    double temperatureC{}; ///< Current thermal proxy in degrees Celsius.
    double depthOfDischarge{}; ///< Current normalized battery depth of discharge.
    double thermalRisk{}; ///< Temperature-only conditional probability for one second.
    double energyPressure{}; ///< Normalized energy pressure in [0, 1].
    double combinedRisk{}; ///< One-second F1 probability after multiplicative energy correction.
    double failureIntensityPerSecond{}; ///< Derived -log(1-pF1_1s), not a tunable intensity.
    double stepFailureProbability{}; ///< Conditional probability for the reference check interval.
    double continuousBusySeconds{}; ///< Uninterrupted busy duration; zero-length transitions do not break it.
    bool busy{}; ///< State to use when advancing the next physical interval.
};

/** Pure F1 thermal, energy, risk, and hazard calculations for one compute node. */
class F1SelfStateFaultModel
{
  public:
    /** Construct a pure model from a strictly validated F1 configuration. */
    explicit F1SelfStateFaultModel(const F1FaultParameters& parameters);

    /** @return Baseline temperature and configured initial depth of discharge. */
    F1SelfStateFaultSnapshot CreateInitialSnapshot() const;

    /**
     * Advance one node by an exact closed-form interval.
     *
     * @param snapshot Mutable node state.
     * @param busy True for heating and compute-energy consumption.
     * @param intervalSeconds Non-negative interval length in seconds.
     * @param probabilityIntervalSeconds Reference probability interval, independent of elapsed time.
     */
    void Update(F1SelfStateFaultSnapshot& snapshot,
                bool busy,
                double intervalSeconds,
                double probabilityIntervalSeconds = 1.0) const;

    /** Refresh probabilities without advancing physical state or consuming RNG. */
    void Evaluate(F1SelfStateFaultSnapshot& snapshot, double probabilityIntervalSeconds = 1.0) const;
    /** Advance using the previous busy state, then install the new state; never sample. */
    void AdvanceTo(F1SelfStateFaultSnapshot& snapshot, int64_t& lastUpdateNs,
                   int64_t nowNs, bool busy, double probabilityIntervalSeconds = 1.0) const;
    /** @return Derived exponential heating time constant in seconds. */
    double GetHeatingTauSeconds() const;
    /** @return Derived linear cooling rate in degrees Celsius per second. */
    double GetCoolingRate() const;
    /** @return Time to base from the protected START temperature, in seconds. */
    double GetRecoveryDurationSeconds(double temperatureC) const;
  private:
    F1FaultParameters m_parameters; ///< Immutable validated F1 parameters.
};

} // namespace ns3

#endif // SATCOMPUTE_F1_SELF_STATE_FAULT_MODEL_H
