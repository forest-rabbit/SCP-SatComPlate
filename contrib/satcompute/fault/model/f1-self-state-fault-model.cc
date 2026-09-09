/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "f1-self-state-fault-model.h"

#include "ns3/abort.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
                            double intervalSeconds,
                            double probabilityIntervalSeconds) const
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
        const double gap = std::max(0.0, temperature.saturationC - snapshot.temperatureC);
        const double shape = temperature.heatingShapeGamma - 1.0;
        const double coefficient = GetHeatingCoefficient();
        // Exact autonomous flow from the current temperature, not from task age.
        // log1p preserves the exponential limit when gamma is close to one.
        const double decay = shape == 0.0
            ? coefficient * intervalSeconds
            : std::log1p(shape * coefficient * intervalSeconds * std::pow(gap, shape)) / shape;
        snapshot.temperatureC = temperature.saturationC - gap * std::exp(-decay);
        snapshot.continuousBusySeconds += intervalSeconds;
    }
    else
    {
        snapshot.temperatureC =
            std::max(temperature.baseC, snapshot.temperatureC - GetCoolingRate() * intervalSeconds);
        snapshot.continuousBusySeconds = 0.0;
    }

    if (m_parameters.energy.enabled && busy)
    {
        snapshot.depthOfDischarge +=
            m_parameters.energy.incrementalComputePowerW * intervalSeconds /
            (3600.0 * m_parameters.energy.batteryWh);
    }

    snapshot.busy = busy;
    Evaluate(snapshot, probabilityIntervalSeconds);
}

double
F1SelfStateFaultModel::GetHeatingCoefficient() const
{
    const auto& t = m_parameters.temperature;
    const double baseGap = t.saturationC - t.baseC;
    const double logRatio = std::log(baseGap / (t.saturationC - t.criticalC));
    const double shape = t.heatingShapeGamma - 1.0;
    return shape == 0.0
        ? logRatio / t.heatingToCriticalSeconds
        : std::pow(baseGap, -shape) * std::expm1(shape * logRatio) /
              (shape * t.heatingToCriticalSeconds);
}

void
F1SelfStateFaultModel::AdvanceTo(F1SelfStateFaultSnapshot& snapshot,
                                int64_t& lastUpdateNs, int64_t nowNs, bool busy,
                                double probabilityIntervalSeconds) const
{
    NS_ABORT_MSG_IF(nowNs < lastUpdateNs, "F1 physical state cannot move backward");
    Update(snapshot, snapshot.busy, static_cast<double>(nowNs - lastUpdateNs) / 1e9,
           probabilityIntervalSeconds);
    snapshot.busy = busy;
    lastUpdateNs = nowNs;
    Evaluate(snapshot, probabilityIntervalSeconds);
}

double
F1SelfStateFaultModel::GetCoolingRate() const
{
    const auto& t = m_parameters.temperature;
    return (t.criticalC - t.baseC) / t.coolingFromCriticalToBaseSeconds;
}

double
F1SelfStateFaultModel::GetRecoveryDurationSeconds(double temperatureC) const
{
    const auto& t = m_parameters.temperature;
    return (std::clamp(temperatureC, t.baseC, t.criticalC) - t.baseC) / GetCoolingRate();
}

void
F1SelfStateFaultModel::Evaluate(F1SelfStateFaultSnapshot& snapshot,
                               double probabilityIntervalSeconds) const
{
    NS_ABORT_MSG_IF(!std::isfinite(probabilityIntervalSeconds) || probabilityIntervalSeconds <= 0,
                    "F1 probability interval must be finite and positive");
    const auto& temperature = m_parameters.temperature;
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
    snapshot.combinedRisk = snapshot.thermalRisk *
        (1.0 + m_parameters.energy.correctionWeight * snapshot.energyPressure);
    snapshot.combinedRisk = ClampUnit(snapshot.combinedRisk);
    snapshot.failureIntensityPerSecond = snapshot.combinedRisk == 1.0
        ? std::numeric_limits<double>::infinity() : -std::log1p(-snapshot.combinedRisk);
    snapshot.stepFailureProbability = snapshot.combinedRisk == 1.0
        ? 1.0 : -std::expm1(-snapshot.failureIntensityPerSecond * probabilityIntervalSeconds);
}

} // namespace ns3
