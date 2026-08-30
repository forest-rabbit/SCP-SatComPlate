/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "self-state-fault-model.h"

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

SelfStateFaultModel::SelfStateFaultModel(const SelfStateFaultConfig& config)
    : m_config(config)
{
}

SelfStateFaultSnapshot
SelfStateFaultModel::CreateInitialSnapshot() const
{
    SelfStateFaultSnapshot snapshot;
    snapshot.temperatureC = m_config.temperature.baseC;
    snapshot.depthOfDischarge = m_config.energy.initialDod;
    return snapshot;
}

void
SelfStateFaultModel::Update(SelfStateFaultSnapshot& snapshot,
                            bool busy,
                            double intervalSeconds) const
{
    NS_ABORT_MSG_IF(!std::isfinite(intervalSeconds) || intervalSeconds <= 0.0,
                    "F1 update interval must be finite and positive");
    const FaultTemperatureConfig& temperature = m_config.temperature;
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

    if (m_config.energy.enabled && busy)
    {
        snapshot.depthOfDischarge +=
            m_config.energy.incrementalComputePowerW * intervalSeconds /
            (3600.0 * m_config.energy.batteryWh);
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
        m_config.energy.enabled
            ? ClampUnit((snapshot.depthOfDischarge - m_config.energy.riskDod) /
                        (m_config.energy.criticalDod - m_config.energy.riskDod))
            : 0.0;
    snapshot.combinedRisk =
        1.0 - (1.0 - snapshot.thermalRisk) *
                  (1.0 - m_config.energy.correctionWeight * snapshot.energyPressure);
    snapshot.combinedRisk = ClampUnit(snapshot.combinedRisk);
    snapshot.failureIntensityPerSecond =
        m_config.maxFailureIntensityPerSecond * snapshot.combinedRisk;
    snapshot.stepFailureProbability =
        -std::expm1(-snapshot.failureIntensityPerSecond * intervalSeconds);
    if (snapshot.temperatureC >= temperature.criticalC)
    {
        snapshot.stepFailureProbability = 1.0;
    }
    snapshot.busy = busy;
}

bool
SelfStateFaultModel::IsRiskActive(const SelfStateFaultSnapshot& snapshot) const
{
    return snapshot.combinedRisk >= m_config.riskThreshold;
}

} // namespace ns3
