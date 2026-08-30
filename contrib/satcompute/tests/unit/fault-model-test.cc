/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/fault-model-config.h"
#include "ns3/self-state-fault-model.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

using Json = nlohmann::json;

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Json
MakeValidConfig()
{
    return {
        {"schema_version", 1},
        {"check_interval_ns", 1000000000},
        {"recoverable_compute_duration_ns", 10000000000},
        {"self_state",
         {{"enabled", true},
          {"temperature",
           {{"base_c", 17.0},
            {"saturation_c", 35.0},
            {"risk_c", 20.0},
            {"critical_c", 30.0},
            {"heating_tau_s", 43.0},
            {"cooling_tau_s", 40.0},
            {"growth_factor", 3.0}}},
          {"energy",
           {{"enabled", true},
            {"initial_dod", 0.25},
            {"risk_dod", 0.30},
            {"critical_dod", 0.50},
            {"battery_wh", 230.0},
            {"incremental_compute_power_w", 2.44},
            {"correction_weight", 0.10}}},
          {"risk_threshold", 0.60},
          {"max_failure_intensity_per_s", 0.01}}},
        {"radiation",
         {{"enabled", false},
          {"longitude_min_deg", -90.0},
          {"longitude_max_deg", 5.0},
          {"latitude_min_deg", -50.0},
          {"latitude_max_deg", 5.0},
          {"effective_failure_intensity_per_s", 0.0},
          {"risk_threshold", 0.01},
          {"reset_exposure_on_exit", true}}},
        {"debris",
         {{"enabled", false},
          {"mode", "fixed_k"},
          {"fixed_count", 0},
          {"single_satellite_intensity_per_s", 0.0}}}};
}

std::filesystem::path
WriteConfig(const std::filesystem::path& directory,
            const std::string& name,
            const Json& config)
{
    const std::filesystem::path path = directory / (name + ".json");
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    Check(output.is_open(), "cannot create fault-model test config");
    output << config.dump(2) << '\n';
    return path;
}

void
ExpectError(const std::filesystem::path& directory,
            const std::string& name,
            const Json& config,
            const std::string& expectedField)
{
    const std::filesystem::path path = WriteConfig(directory, name, config);
    try
    {
        ReadFaultModelConfig(path);
    }
    catch (const FaultModelConfigError& error)
    {
        const std::string message = error.what();
        Check(message.find(std::filesystem::absolute(path).lexically_normal().string()) !=
                      std::string::npos &&
                  message.find(expectedField) != std::string::npos,
              "fault-model error omitted path or field: " + message);
        return;
    }
    throw std::runtime_error("invalid fault-model config was accepted: " + name);
}

void
CheckValid(const std::filesystem::path& directory)
{
    const FaultModelConfig config =
        ReadFaultModelConfig(WriteConfig(directory, "valid", MakeValidConfig()));
    Check(config.schemaVersion == 1 && config.checkIntervalNs == 1000000000 &&
              config.recoverableComputeDurationNs == 10000000000,
          "fault-model root fields differ");
    Check(config.selfState.enabled && config.selfState.temperature.baseC == 17.0 &&
              config.selfState.temperature.heatingTauSeconds == 43.0 &&
              config.selfState.energy.initialDod == 0.25 &&
              config.selfState.maxFailureIntensityPerSecond == 0.01,
          "self-state config fields differ");
    Check(!config.radiation.enabled &&
              config.radiation.longitudeMinDegrees == -90.0 &&
              config.radiation.resetExposureOnExit && !config.debris.enabled &&
              config.debris.mode == "fixed_k",
          "F2/F3 config fields differ");
}

void
CheckInvalid(const std::filesystem::path& directory)
{
    Json value = MakeValidConfig();
    value["unknown"] = true;
    ExpectError(directory, "unknown-root", value, "root");
    value = MakeValidConfig();
    value["schema_version"] = 2;
    ExpectError(directory, "schema", value, "schema_version");
    value = MakeValidConfig();
    value["check_interval_ns"] = 0;
    ExpectError(directory, "zero-check", value, "check_interval_ns");
    value = MakeValidConfig();
    value["self_state"]["temperature"]["risk_c"] = 31.0;
    ExpectError(directory, "temperature-order", value, "self_state.temperature");
    value = MakeValidConfig();
    value["self_state"]["temperature"]["heating_tau_s"] = 0.0;
    ExpectError(directory, "heating-tau", value, "self_state.temperature");
    value = MakeValidConfig();
    value["self_state"]["temperature"]["heating_tau_s"] =
        std::numeric_limits<double>::infinity();
    ExpectError(directory, "non-finite-heating-tau", value, "heating_tau_s");
    value = MakeValidConfig();
    value["self_state"]["energy"]["initial_dod"] = 0.4;
    ExpectError(directory, "dod-order", value, "self_state.energy");
    value = MakeValidConfig();
    value["self_state"]["risk_threshold"] = 1.1;
    ExpectError(directory, "risk-threshold", value, "risk_threshold");
    value = MakeValidConfig();
    value["self_state"]["max_failure_intensity_per_s"] = -0.1;
    ExpectError(directory,
                "negative-f1-intensity",
                value,
                "max_failure_intensity_per_s");
    value = MakeValidConfig();
    value["radiation"]["longitude_min_deg"] = 10.0;
    ExpectError(directory, "radiation-region", value, "radiation");
    value = MakeValidConfig();
    value["radiation"]["reset_exposure_on_exit"] = false;
    ExpectError(directory, "radiation-reset", value, "reset_exposure_on_exit");
    value = MakeValidConfig();
    value["debris"]["mode"] = "fixed_k";
    value["debris"]["single_satellite_intensity_per_s"] = 0.1;
    ExpectError(directory, "debris-conflict", value, "debris");
    value = MakeValidConfig();
    value["debris"]["enabled"] = true;
    ExpectError(directory, "enabled-empty-debris", value, "debris");
}

void
CheckF1Model(const std::filesystem::path& directory)
{
    const FaultModelConfig config =
        ReadFaultModelConfig(WriteConfig(directory, "f1-model", MakeValidConfig()));
    const SelfStateFaultModel model(config.selfState);

    SelfStateFaultSnapshot unchanged = model.CreateInitialSnapshot();
    const SelfStateFaultSnapshot initial = unchanged;
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

    SelfStateFaultSnapshot idle = model.CreateInitialSnapshot();
    model.Update(idle, false, 120.0);
    Check(idle.temperatureC == config.selfState.temperature.baseC &&
              idle.depthOfDischarge == config.selfState.energy.initialDod &&
              idle.combinedRisk == 0.0 && idle.stepFailureProbability == 0.0 &&
              !model.IsRiskActive(idle),
          "idle F1 state changed from its baseline");

    SelfStateFaultSnapshot singleTask = model.CreateInitialSnapshot();
    model.Update(singleTask, true, 10.0);
    Check(singleTask.temperatureC > 20.0 && singleTask.temperatureC < 21.0 &&
              singleTask.temperatureC < config.selfState.temperature.criticalC &&
              !model.IsRiskActive(singleTask) &&
              singleTask.stepFailureProbability < singleTask.combinedRisk,
          "one representative task produced an invalid F1 state");
    const double taskEndTemperature = singleTask.temperatureC;
    model.Update(singleTask, false, 1.0);
    Check(singleTask.temperatureC < taskEndTemperature &&
              singleTask.temperatureC > config.selfState.temperature.baseC,
          "task completion reset F1 temperature instead of cooling continuously");

    SelfStateFaultSnapshot continuous = model.CreateInitialSnapshot();
    for (int second = 0; second < 55; ++second)
    {
        model.Update(continuous, true, 1.0);
    }
    const double expectedTemperature =
        config.selfState.temperature.saturationC -
        (config.selfState.temperature.saturationC -
         config.selfState.temperature.baseC) *
            std::exp(-55.0 / config.selfState.temperature.heatingTauSeconds);
    Check(std::abs(continuous.temperatureC - expectedTemperature) < 1e-12 &&
              continuous.temperatureC > 29.8 && continuous.temperatureC < 30.1 &&
              model.IsRiskActive(continuous) &&
              continuous.stepFailureProbability > 0.0 &&
              continuous.stepFailureProbability <= 1.0,
          "55-second continuous load missed the calibrated F1 target");
    Check(continuous.depthOfDischarge > config.selfState.energy.initialDod &&
              continuous.energyPressure == 0.0,
          "small F1 energy correction changed too quickly");

    model.Update(continuous, true, 5.0);
    Check(continuous.temperatureC >= config.selfState.temperature.criticalC &&
              continuous.stepFailureProbability == 1.0,
          "critical F1 temperature did not force deterministic shutdown");
    model.Update(continuous, false, 120.0);
    Check(continuous.temperatureC < config.selfState.temperature.riskC &&
              !model.IsRiskActive(continuous) &&
              continuous.stepFailureProbability == 0.0,
          "F1 cooling did not leave the risk region");

    SelfStateFaultSnapshot monotonic = model.CreateInitialSnapshot();
    double previousTemperature = monotonic.temperatureC;
    double previousRisk = monotonic.thermalRisk;
    for (int second = 0; second < 60; ++second)
    {
        model.Update(monotonic, true, 1.0);
        Check(monotonic.temperatureC >= previousTemperature &&
                  monotonic.temperatureC <=
                      config.selfState.temperature.saturationC &&
                  monotonic.thermalRisk >= previousRisk &&
                  monotonic.thermalRisk >= 0.0 && monotonic.thermalRisk <= 1.0 &&
                  monotonic.combinedRisk >= 0.0 && monotonic.combinedRisk <= 1.0,
              "F1 heating or risk is not bounded and monotonic");
        previousTemperature = monotonic.temperatureC;
        previousRisk = monotonic.thermalRisk;
    }

    FaultModelConfig noEnergyConfig = config;
    noEnergyConfig.selfState.energy.enabled = false;
    const SelfStateFaultModel noEnergyModel(noEnergyConfig.selfState);
    SelfStateFaultSnapshot noEnergy = noEnergyModel.CreateInitialSnapshot();
    noEnergyModel.Update(noEnergy, true, 30.0);
    Check(noEnergy.energyPressure == 0.0 &&
              std::abs(noEnergy.combinedRisk - noEnergy.thermalRisk) < 1e-15,
          "disabled F1 energy term changed thermal risk");

    FaultModelConfig energyConfig = config;
    energyConfig.selfState.energy.initialDod = 0.30;
    energyConfig.selfState.energy.riskDod = 0.30;
    const SelfStateFaultModel energyModel(energyConfig.selfState);
    SelfStateFaultSnapshot energy = energyModel.CreateInitialSnapshot();
    energyModel.Update(energy, true, 3600.0);
    const double expectedDod =
        energyConfig.selfState.energy.initialDod +
        energyConfig.selfState.energy.incrementalComputePowerW /
            energyConfig.selfState.energy.batteryWh;
    Check(std::abs(energy.depthOfDischarge - expectedDod) < 1e-12,
          "F1 W/Wh/s energy conversion differs");
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Temporary fault-model test directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        Check(!outputDirectory.empty(), "outputDir is required");
        std::filesystem::create_directories(outputDirectory);
        CheckValid(outputDirectory);
        CheckInvalid(outputDirectory);
        CheckF1Model(outputDirectory);
        std::cout << "SatCompute fault model tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
