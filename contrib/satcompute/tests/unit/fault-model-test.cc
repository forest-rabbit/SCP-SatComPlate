/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/fault-parameter-validator.h"
#include "ns3/fault-para.h"
#include "ns3/f1-self-state-fault-model.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void
ExpectError(const FaultParameters& parameters,
            const std::string& expectedField,
            const std::string& name)
{
    try
    {
        ValidateFaultParameters(parameters);
    }
    catch (const FaultParameterError& error)
    {
        Check(std::string(error.what()).find(expectedField) != std::string::npos,
              "fault-parameter error omitted field for " + name + ": " + error.what());
        return;
    }
    throw std::runtime_error("invalid fault parameters were accepted: " + name);
}

void
CheckDefaults()
{
    const FaultParameters parameters = GetDefaultFaultParameters();
    Check(parameters.checkIntervalSeconds == 1.0 &&
              parameters.recoverableComputeDurationSeconds == 8.0,
          "fault common defaults differ");
    Check(parameters.f1.enabled && parameters.f1.temperature.baseC == 17.0 &&
              parameters.f1.temperature.heatingTauSeconds == 43.0 &&
              parameters.f1.temperature.coolingTauSeconds == 40.0 &&
              parameters.f1.energy.initialDod == 0.25 &&
              parameters.f1.maxFailureIntensityPerSecond == 0.005,
          "F1 defaults differ");
    Check(!parameters.f2.enabled &&
              parameters.f2.longitudeMinDegrees == -90.0 &&
              parameters.f2.resetExposureOnExit && !parameters.f3.enabled &&
              parameters.f3.mode == "fixed_k" && parameters.f3.fixedCount == 1,
          "F2/F3 defaults differ");
}

void
CheckInvalid()
{
    FaultParameters value = GetDefaultFaultParameters();
    value.checkIntervalSeconds = 0.0;
    ExpectError(value, "common", "zero-check-interval");

    value = GetDefaultFaultParameters();
    value.recoverableComputeDurationSeconds =
        std::numeric_limits<double>::infinity();
    ExpectError(value, "common.recoverable", "non-finite-recovery");

    value = GetDefaultFaultParameters();
    value.f1.temperature.riskC = 31.0;
    ExpectError(value, "F1.temperature", "temperature-order");

    value = GetDefaultFaultParameters();
    value.f1.temperature.heatingTauSeconds = 0.0;
    ExpectError(value, "F1.temperature", "heating-tau");

    value = GetDefaultFaultParameters();
    value.f1.temperature.heatingTauSeconds =
        std::numeric_limits<double>::infinity();
    ExpectError(value, "F1.temperature.heating_tau_s", "non-finite-heating-tau");

    value = GetDefaultFaultParameters();
    value.f1.energy.initialDod = 0.4;
    ExpectError(value, "F1.energy", "dod-order");

    value = GetDefaultFaultParameters();
    value.f1.riskThreshold = 1.1;
    ExpectError(value, "F1.risk_threshold", "risk-threshold");

    value = GetDefaultFaultParameters();
    value.f1.maxFailureIntensityPerSecond = -0.1;
    ExpectError(value, "F1.max_failure_intensity_per_s", "negative-F1-intensity");

    value = GetDefaultFaultParameters();
    value.f2.longitudeMinDegrees = 10.0;
    ExpectError(value, "F2.region", "F2-region");

    value = GetDefaultFaultParameters();
    value.f2.resetExposureOnExit = false;
    ExpectError(value, "F2.reset_exposure_on_exit", "F2-reset");

    value = GetDefaultFaultParameters();
    value.f3.singleSatelliteIntensityPerSecond = 0.1;
    ExpectError(value, "F3", "F3-mode-conflict");

    value = GetDefaultFaultParameters();
    value.f3.enabled = true;
    value.f3.fixedCount = 0;
    ExpectError(value, "F3", "enabled-empty-F3");
}

void
CheckF1Model()
{
    const FaultParameters parameters = GetDefaultFaultParameters();
    const F1SelfStateFaultModel model(parameters.f1);

    F1SelfStateFaultSnapshot unchanged = model.CreateInitialSnapshot();
    const F1SelfStateFaultSnapshot initial = unchanged;
    model.Update(unchanged, true, 0.0);
    Check(unchanged.temperatureC == initial.temperatureC &&
              unchanged.depthOfDischarge == initial.depthOfDischarge &&
              unchanged.thermalRisk == initial.thermalRisk &&
              unchanged.energyPressure == initial.energyPressure &&
              unchanged.combinedRisk == initial.combinedRisk &&
              unchanged.failureIntensityPerSecond ==
                  initial.failureIntensityPerSecond &&
              unchanged.stepFailureProbability ==
                  initial.stepFailureProbability &&
              unchanged.busy == initial.busy,
          "zero-duration F1 update changed state");

    F1SelfStateFaultSnapshot idle = model.CreateInitialSnapshot();
    model.Update(idle, false, 120.0);
    Check(idle.temperatureC == parameters.f1.temperature.baseC &&
              idle.depthOfDischarge == parameters.f1.energy.initialDod &&
              idle.combinedRisk == 0.0 && idle.stepFailureProbability == 0.0 &&
              !model.IsRiskActive(idle),
          "idle F1 state changed from its baseline");

    F1SelfStateFaultSnapshot singleTask = model.CreateInitialSnapshot();
    model.Update(singleTask, true, 10.0);
    Check(singleTask.temperatureC > 20.0 && singleTask.temperatureC < 21.0 &&
              singleTask.temperatureC < parameters.f1.temperature.criticalC &&
              !model.IsRiskActive(singleTask) &&
              singleTask.stepFailureProbability < singleTask.combinedRisk,
          "one representative task produced an invalid F1 state");
    const double taskEndTemperature = singleTask.temperatureC;
    model.Update(singleTask, false, 1.0);
    Check(singleTask.temperatureC < taskEndTemperature &&
              singleTask.temperatureC > parameters.f1.temperature.baseC,
          "task completion reset F1 temperature instead of cooling continuously");

    F1SelfStateFaultSnapshot continuous = model.CreateInitialSnapshot();
    for (int second = 0; second < 55; ++second)
    {
        model.Update(continuous, true, 1.0);
    }
    const double expectedTemperature =
        parameters.f1.temperature.saturationC -
        (parameters.f1.temperature.saturationC -
         parameters.f1.temperature.baseC) *
            std::exp(-55.0 / parameters.f1.temperature.heatingTauSeconds);
    Check(std::abs(continuous.temperatureC - expectedTemperature) < 1e-12 &&
              continuous.temperatureC > 29.8 && continuous.temperatureC < 30.1 &&
              model.IsRiskActive(continuous) &&
              continuous.stepFailureProbability > 0.0 &&
              continuous.stepFailureProbability <= 1.0,
          "55-second continuous load missed the calibrated F1 target");
    Check(continuous.depthOfDischarge > parameters.f1.energy.initialDod &&
              continuous.energyPressure == 0.0,
          "small F1 energy correction changed too quickly");

    model.Update(continuous, true, 5.0);
    Check(continuous.temperatureC >= parameters.f1.temperature.criticalC &&
              continuous.stepFailureProbability == 1.0,
          "critical F1 temperature did not force deterministic shutdown");

    F1SelfStateFaultSnapshot recovery = model.CreateInitialSnapshot();
    recovery.temperatureC = parameters.f1.temperature.criticalC;
    model.Update(recovery, false, parameters.recoverableComputeDurationSeconds);
    Check(recovery.temperatureC > 27.6 && recovery.temperatureC < 27.7 &&
              recovery.combinedRisk < parameters.f1.riskThreshold &&
              !model.IsRiskActive(recovery),
          "8-second recovery did not cool F1 below its notice threshold");

    model.Update(continuous, false, 120.0);
    Check(continuous.temperatureC < parameters.f1.temperature.riskC &&
              !model.IsRiskActive(continuous) &&
              continuous.stepFailureProbability == 0.0,
          "F1 cooling did not leave the risk region");

    F1SelfStateFaultSnapshot monotonic = model.CreateInitialSnapshot();
    double previousTemperature = monotonic.temperatureC;
    double previousRisk = monotonic.thermalRisk;
    for (int second = 0; second < 60; ++second)
    {
        model.Update(monotonic, true, 1.0);
        Check(monotonic.temperatureC >= previousTemperature &&
                  monotonic.temperatureC <=
                      parameters.f1.temperature.saturationC &&
                  monotonic.thermalRisk >= previousRisk &&
                  monotonic.thermalRisk >= 0.0 && monotonic.thermalRisk <= 1.0 &&
                  monotonic.combinedRisk >= 0.0 && monotonic.combinedRisk <= 1.0,
              "F1 heating or risk is not bounded and monotonic");
        previousTemperature = monotonic.temperatureC;
        previousRisk = monotonic.thermalRisk;
    }

    FaultParameters noEnergyParameters = parameters;
    noEnergyParameters.f1.energy.enabled = false;
    const F1SelfStateFaultModel noEnergyModel(noEnergyParameters.f1);
    F1SelfStateFaultSnapshot noEnergy = noEnergyModel.CreateInitialSnapshot();
    noEnergyModel.Update(noEnergy, true, 30.0);
    Check(noEnergy.energyPressure == 0.0 &&
              std::abs(noEnergy.combinedRisk - noEnergy.thermalRisk) < 1e-15,
          "disabled F1 energy term changed thermal risk");

    FaultParameters energyParameters = parameters;
    energyParameters.f1.energy.initialDod = 0.30;
    energyParameters.f1.energy.riskDod = 0.30;
    const F1SelfStateFaultModel energyModel(energyParameters.f1);
    F1SelfStateFaultSnapshot energy = energyModel.CreateInitialSnapshot();
    energyModel.Update(energy, true, 3600.0);
    const double expectedDod =
        energyParameters.f1.energy.initialDod +
        energyParameters.f1.energy.incrementalComputePowerW /
            energyParameters.f1.energy.batteryWh;
    Check(std::abs(energy.depthOfDischarge - expectedDod) < 1e-12,
          "F1 W/Wh/s energy conversion differs");
}

} // namespace

int
main()
{
    try
    {
        CheckDefaults();
        CheckInvalid();
        CheckF1Model();
        std::cout << "SatCompute fault model tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
