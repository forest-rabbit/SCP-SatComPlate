/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "f1-self-state-fault-model.h"

#include "ns3/abort.h"

#include <algorithm>
#include <cmath>

namespace ns3
{

namespace
{

double
ClampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

F1SelfStateFaultModel::F1SelfStateFaultModel(const F1FaultParameters& parameters)
    : m_parameters(parameters)
{
}

F1SelfStateFaultSnapshot
F1SelfStateFaultModel::CreateInitialSnapshot() const
{
    F1SelfStateFaultSnapshot snapshot;
    snapshot.temperatureC = m_parameters.temperature.baseC;
    snapshot.depthOfDischarge = m_parameters.energy.initialDod;
    return snapshot;
}

void
F1SelfStateFaultModel::Update(F1SelfStateFaultSnapshot& snapshot,
                            bool busy,
                            double intervalSeconds) const
{
    NS_ABORT_MSG_IF(!std::isfinite(intervalSeconds) || intervalSeconds < 0.0,
                    "F1 update interval must be finite and non-negative");
    if (intervalSeconds == 0.0)
    {
        return;
    }
    const F1TemperatureParameters& temperature = m_parameters.temperature;
    if (busy)
    {
        snapshot.temperatureC =
            temperature.saturationC -
            (temperature.saturationC - snapshot.temperatureC) *
                std::exp(-intervalSeconds / temperature.heatingTauSeconds);
    }
    else
    {
        snapshot.temperatureC =
            temperature.baseC +
            (snapshot.temperatureC - temperature.baseC) *
                std::exp(-intervalSeconds / temperature.coolingTauSeconds);
    }

    if (m_parameters.energy.enabled && busy)
    {
        snapshot.depthOfDischarge +=
            m_parameters.energy.incrementalComputePowerW * intervalSeconds /
            (3600.0 * m_parameters.energy.batteryWh);
    }

    const double thermalPosition = ClampUnit(
        (snapshot.temperatureC - temperature.riskC) /
        (temperature.criticalC - temperature.riskC));
    if (snapshot.temperatureC <= temperature.riskC)
    {
        snapshot.thermalRisk = 0.0;
    }
    else if (snapshot.temperatureC >= temperature.criticalC)
    {
        snapshot.thermalRisk = 1.0;
    }
    else
    {
        snapshot.thermalRisk =
            std::expm1(temperature.growthFactor * thermalPosition) /
            std::expm1(temperature.growthFactor);
    }

    snapshot.energyPressure =
        m_parameters.energy.enabled
            ? ClampUnit((snapshot.depthOfDischarge - m_parameters.energy.riskDod) /
                        (m_parameters.energy.criticalDod - m_parameters.energy.riskDod))
            : 0.0;
    snapshot.combinedRisk =
        1.0 - (1.0 - snapshot.thermalRisk) *
                  (1.0 - m_parameters.energy.correctionWeight * snapshot.energyPressure);
    snapshot.combinedRisk = ClampUnit(snapshot.combinedRisk);
    snapshot.failureIntensityPerSecond =
        m_parameters.maxFailureIntensityPerSecond * snapshot.combinedRisk;
    snapshot.stepFailureProbability =
        -std::expm1(-snapshot.failureIntensityPerSecond * intervalSeconds);
    if (snapshot.temperatureC >= temperature.criticalC)
    {
        snapshot.stepFailureProbability = 1.0;
    }
    snapshot.busy = busy;
}

bool
F1SelfStateFaultModel::IsRiskActive(const F1SelfStateFaultSnapshot& snapshot) const
{
    return snapshot.combinedRisk >= m_parameters.riskThreshold;
}

} // namespace ns3
