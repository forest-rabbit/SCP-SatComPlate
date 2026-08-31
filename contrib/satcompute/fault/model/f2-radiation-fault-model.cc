/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "f2-radiation-fault-model.h"

#include "ns3/abort.h"
#include "ns3/geographic-positions.h"

#include <cmath>

namespace ns3
{

F2RadiationFaultModel::F2RadiationFaultModel(const F2FaultParameters& parameters)
    : m_parameters(parameters)
{
}

F2RadiationFaultSnapshot
F2RadiationFaultModel::CreateInitialSnapshot() const
{
    return {};
}

void
F2RadiationFaultModel::Update(F2RadiationFaultSnapshot& snapshot,
                              const Vector& ecefPositionM,
                              double intervalSeconds) const
{
    NS_ABORT_MSG_IF(!std::isfinite(intervalSeconds) || intervalSeconds < 0.0,
                    "F2 update interval must be finite and non-negative");
    NS_ABORT_MSG_IF(!std::isfinite(ecefPositionM.x) ||
                        !std::isfinite(ecefPositionM.y) ||
                        !std::isfinite(ecefPositionM.z) ||
                        (ecefPositionM.x == 0.0 && ecefPositionM.y == 0.0 &&
                         ecefPositionM.z == 0.0),
                    "F2 position must be a finite non-zero ECEF vector");

    Vector geographic;
    if (ecefPositionM.x == 0.0 && ecefPositionM.y == 0.0)
    {
        // The upstream iterative converter divides by cos(latitude) at the
        // exact poles. Keep one explicit pole guard and reuse it everywhere
        // through this model rather than duplicating a general conversion.
        geographic = Vector(std::copysign(90.0, ecefPositionM.z), 0.0, 0.0);
    }
    else
    {
        geographic = GeographicPositions::CartesianToGeographicCoordinates(
            ecefPositionM,
            GeographicPositions::SPHERE);
    }
    const bool wasInRegion = snapshot.inRegion;
    snapshot.latitudeDegrees = geographic.x;
    snapshot.longitudeDegrees = geographic.y;
    snapshot.inRegion =
        snapshot.longitudeDegrees >= m_parameters.longitudeMinDegrees &&
        snapshot.longitudeDegrees <= m_parameters.longitudeMaxDegrees &&
        snapshot.latitudeDegrees >= m_parameters.latitudeMinDegrees &&
        snapshot.latitudeDegrees <= m_parameters.latitudeMaxDegrees;

    if (!snapshot.inRegion)
    {
        snapshot.continuousExposureSeconds = 0.0;
        snapshot.cumulativeFailureHazard = 0.0;
        snapshot.cumulativeFailureProbability = 0.0;
        snapshot.spatialRisk = 0.0;
        snapshot.seuIntensityPerSecond = 0.0;
        snapshot.failureIntensityPerSecond = 0.0;
        snapshot.stepFailureProbability = 0.0;
        return;
    }

    if (!wasInRegion)
    {
        snapshot.continuousExposureSeconds = 0.0;
        snapshot.cumulativeFailureHazard = 0.0;
    }
    else
    {
        snapshot.continuousExposureSeconds += intervalSeconds;
    }

    const double longitudeOffset =
        (snapshot.longitudeDegrees - m_parameters.hotspotLongitudeDegrees) /
        m_parameters.sigmaLongitudeDegrees;
    const double latitudeOffset =
        (snapshot.latitudeDegrees - m_parameters.hotspotLatitudeDegrees) /
        m_parameters.sigmaLatitudeDegrees;
    snapshot.spatialRisk =
        std::exp(-0.5 * (longitudeOffset * longitudeOffset +
                         latitudeOffset * latitudeOffset));
    snapshot.seuIntensityPerSecond =
        m_parameters.referenceSeuIntensityPerSecond * snapshot.spatialRisk;
    snapshot.failureIntensityPerSecond =
        m_parameters.seuToComputeFailureProbability *
        snapshot.seuIntensityPerSecond;
    snapshot.stepFailureProbability =
        -std::expm1(-snapshot.failureIntensityPerSecond * intervalSeconds);
    snapshot.cumulativeFailureHazard +=
        snapshot.failureIntensityPerSecond * intervalSeconds;
    snapshot.cumulativeFailureProbability =
        -std::expm1(-snapshot.cumulativeFailureHazard);
}

bool
F2RadiationFaultModel::IsRiskActive(const F2RadiationFaultSnapshot& snapshot) const
{
    return snapshot.inRegion &&
           snapshot.spatialRisk >= m_parameters.spatialRiskThreshold;
}

double
F2RadiationFaultModel::GetMaximumFailureIntensityPerSecond() const
{
    return m_parameters.referenceSeuIntensityPerSecond *
           m_parameters.seuToComputeFailureProbability;
}

} // namespace ns3
