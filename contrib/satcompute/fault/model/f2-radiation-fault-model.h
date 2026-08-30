/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_F2_RADIATION_FAULT_MODEL_H
#define SATCOMPUTE_F2_RADIATION_FAULT_MODEL_H

#include "ns3/fault-para.h"
#include "ns3/vector.h"

namespace ns3
{

/** Complete F2 geographic exposure state for one satellite. */
struct F2RadiationFaultSnapshot
{
    double latitudeDegrees{}; ///< Current geocentric latitude in degrees.
    double longitudeDegrees{}; ///< Current longitude in [-180, 180) degrees.
    double continuousExposureSeconds{}; ///< Current uninterrupted region exposure.
    double cumulativeRisk{}; ///< At-least-one-event risk for this exposure episode.
    double failureIntensityPerSecond{}; ///< Current effective service-failure intensity.
    double stepFailureProbability{}; ///< Conditional probability for the latest interval.
    bool inRegion{}; ///< Whether the current ECEF position is inside the F2 rectangle.
};

/** Pure F2 ECEF, region, continuous-exposure, risk, and hazard calculations. */
class F2RadiationFaultModel
{
  public:
    /** Construct a pure model from a strictly validated F2 configuration. */
    explicit F2RadiationFaultModel(const F2FaultParameters& parameters);

    /** @return An outside-region snapshot with zero exposure and risk. */
    F2RadiationFaultSnapshot CreateInitialSnapshot() const;

    /**
     * Observe one ECEF position and advance the current exposure episode.
     *
     * A transition from outside to inside starts at zero accumulated exposure.
     * Each following inside sample adds the exact interval. The current-step
     * probability uses only the interval intensity, never cumulative risk.
     *
     * @param snapshot Mutable satellite exposure state.
     * @param ecefPositionM Current ns-3 mobility position in ECEF meters.
     * @param intervalSeconds Non-negative interval since the preceding sample.
     */
    void Update(F2RadiationFaultSnapshot& snapshot,
                const Vector& ecefPositionM,
                double intervalSeconds) const;

    /**
     * Test the configured cumulative-risk notice threshold.
     *
     * @param snapshot Current satellite exposure state.
     * @return True when the satellite is inside and cumulative risk reached the threshold.
     */
    bool IsRiskActive(const F2RadiationFaultSnapshot& snapshot) const;

  private:
    F2FaultParameters m_parameters; ///< Immutable validated F2 parameters.
};

} // namespace ns3

#endif // SATCOMPUTE_F2_RADIATION_FAULT_MODEL_H
