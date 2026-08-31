/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_F2_RADIATION_FAULT_MODEL_H
#define SATCOMPUTE_F2_RADIATION_FAULT_MODEL_H

#include "ns3/fault-para.h"
#include "ns3/vector.h"

namespace ns3
{

/** Complete F2 spatial-radiation state for one satellite. */
struct F2RadiationFaultSnapshot
{
    double latitudeDegrees{}; ///< Current geocentric latitude in degrees.
    double longitudeDegrees{}; ///< Current longitude in [-180, 180) degrees.
    double continuousExposureSeconds{}; ///< Current uninterrupted region exposure.
    double cumulativeFailureHazard{}; ///< Integrated compute-failure hazard this pass.
    double cumulativeFailureProbability{}; ///< At-least-one-failure pass statistic.
    double spatialRisk{}; ///< Current dimensionless two-piece Gaussian risk score.
    double seuIntensityPerSecond{}; ///< Current modeled raw SEU intensity.
    double failureIntensityPerSecond{}; ///< Current effective service-failure intensity.
    double stepFailureProbability{}; ///< Conditional probability for the latest interval.
    bool inRegion{}; ///< Whether the current ECEF position is inside the F2 rectangle.
};

/** Pure F2 ECEF, spatial-risk, SEU-mapping, and hazard calculations. */
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
     * Current position alone determines spatial risk and current-step failure
     * intensity. Continuous exposure and integrated pass probability remain
     * statistics and never drive the current risk or notice state.
     *
     * @param snapshot Mutable satellite exposure state.
     * @param ecefPositionM Current ns-3 mobility position in ECEF meters.
     * @param intervalSeconds Non-negative interval since the preceding sample.
     */
    void Update(F2RadiationFaultSnapshot& snapshot,
                const Vector& ecefPositionM,
                double intervalSeconds) const;

    /**
     * Test the configured current spatial-risk notice threshold.
     *
     * @param snapshot Current satellite exposure state.
     * @return True when the current in-region spatial risk reached the threshold.
     */
    bool IsRiskActive(const F2RadiationFaultSnapshot& snapshot) const;

    /**
     * Return the maximum effective compute-failure intensity at the hotspot.
     *
     * @return Product of reference SEU intensity and SEU-to-failure probability.
     */
    double GetMaximumFailureIntensityPerSecond() const;

  private:
    F2FaultParameters m_parameters; ///< Immutable validated F2 parameters.
};

} // namespace ns3

#endif // SATCOMPUTE_F2_RADIATION_FAULT_MODEL_H
