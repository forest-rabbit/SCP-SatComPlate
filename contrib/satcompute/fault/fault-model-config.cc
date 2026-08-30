/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-model-config.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

[[noreturn]] void
Fail(const std::filesystem::path& filename,
     std::string_view field,
     std::string_view message)
{
    throw FaultModelConfigError(filename.string() + ": " + std::string(field) + " " +
                                std::string(message));
}

Json
ReadJson(const std::filesystem::path& filename)
{
    std::ifstream input(filename);
    if (!input.is_open())
    {
        Fail(filename, "file", "cannot be opened");
    }
    try
    {
        return Json::parse(input, nullptr, true, false);
    }
    catch (const std::exception& error)
    {
        Fail(filename, "JSON", error.what());
    }
}

void
RequireObjectFields(const Json& value,
                    const std::filesystem::path& filename,
                    std::string_view name,
                    std::initializer_list<std::string_view> fields)
{
    if (!value.is_object())
    {
        Fail(filename, name, "must be an object");
    }
    std::set<std::string> expected;
    for (const std::string_view field : fields)
    {
        expected.emplace(field);
    }
    std::set<std::string> actual;
    for (const auto& item : value.items())
    {
        actual.insert(item.key());
    }
    if (actual != expected)
    {
        Fail(filename, name, "has missing or unknown fields");
    }
}

const Json&
GetField(const Json& object,
         const std::filesystem::path& filename,
         std::string_view field)
{
    const auto item = object.find(std::string(field));
    if (item == object.end())
    {
        Fail(filename, field, "is missing");
    }
    return *item;
}

bool
RequireBool(const Json& value,
            const std::filesystem::path& filename,
            std::string_view field)
{
    if (!value.is_boolean())
    {
        Fail(filename, field, "must be a boolean");
    }
    return value.get<bool>();
}

double
RequireFiniteNumber(const Json& value,
                    const std::filesystem::path& filename,
                    std::string_view field)
{
    if (!value.is_number())
    {
        Fail(filename, field, "must be a finite number");
    }
    double parsed;
    try
    {
        parsed = value.get<double>();
    }
    catch (const std::exception& error)
    {
        Fail(filename, field, error.what());
    }
    if (!std::isfinite(parsed))
    {
        Fail(filename, field, "must be a finite number");
    }
    return parsed;
}

uint64_t
RequireUint64(const Json& value,
              const std::filesystem::path& filename,
              std::string_view field)
{
    if (value.is_number_unsigned())
    {
        return value.get<uint64_t>();
    }
    if (value.is_number_integer())
    {
        const int64_t parsed = value.get<int64_t>();
        if (parsed >= 0)
        {
            return static_cast<uint64_t>(parsed);
        }
    }
    Fail(filename, field, "must be a non-negative integer");
}

uint32_t
RequireUint32(const Json& value,
              const std::filesystem::path& filename,
              std::string_view field)
{
    const uint64_t parsed = RequireUint64(value, filename, field);
    if (parsed > std::numeric_limits<uint32_t>::max())
    {
        Fail(filename, field, "exceeds uint32 range");
    }
    return static_cast<uint32_t>(parsed);
}

int64_t
RequirePositiveInt64(const Json& value,
                     const std::filesystem::path& filename,
                     std::string_view field)
{
    const uint64_t parsed = RequireUint64(value, filename, field);
    if (parsed == 0 ||
        parsed > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        Fail(filename, field, "must be a positive signed integer");
    }
    return static_cast<int64_t>(parsed);
}

void
RequireRange(double value,
             double minimum,
             double maximum,
             const std::filesystem::path& filename,
             std::string_view field)
{
    if (value < minimum || value > maximum)
    {
        Fail(filename, field, "is outside the permitted range");
    }
}

FaultTemperatureConfig
ParseTemperature(const Json& value, const std::filesystem::path& filename)
{
    RequireObjectFields(value,
                        filename,
                        "self_state.temperature",
                        {"base_c",
                         "saturation_c",
                         "risk_c",
                         "critical_c",
                         "heating_tau_s",
                         "cooling_tau_s",
                         "growth_factor"});
    FaultTemperatureConfig config;
    config.baseC = RequireFiniteNumber(GetField(value, filename, "base_c"),
                                       filename,
                                       "base_c");
    config.saturationC = RequireFiniteNumber(
        GetField(value, filename, "saturation_c"), filename, "saturation_c");
    config.riskC = RequireFiniteNumber(GetField(value, filename, "risk_c"),
                                       filename,
                                       "risk_c");
    config.criticalC = RequireFiniteNumber(GetField(value, filename, "critical_c"),
                                           filename,
                                           "critical_c");
    config.heatingTauSeconds = RequireFiniteNumber(
        GetField(value, filename, "heating_tau_s"), filename, "heating_tau_s");
    config.coolingTauSeconds = RequireFiniteNumber(
        GetField(value, filename, "cooling_tau_s"), filename, "cooling_tau_s");
    config.growthFactor = RequireFiniteNumber(
        GetField(value, filename, "growth_factor"), filename, "growth_factor");
    if (!(config.baseC < config.riskC && config.riskC < config.criticalC &&
          config.criticalC < config.saturationC))
    {
        Fail(filename,
             "self_state.temperature",
             "must satisfy base_c < risk_c < critical_c < saturation_c");
    }
    if (config.heatingTauSeconds <= 0.0 || config.coolingTauSeconds <= 0.0 ||
        config.growthFactor <= 0.0)
    {
        Fail(filename, "self_state.temperature", "time constants and growth must be positive");
    }
    return config;
}

FaultEnergyConfig
ParseEnergy(const Json& value, const std::filesystem::path& filename)
{
    RequireObjectFields(value,
                        filename,
                        "self_state.energy",
                        {"enabled",
                         "initial_dod",
                         "risk_dod",
                         "critical_dod",
                         "battery_wh",
                         "incremental_compute_power_w",
                         "correction_weight"});
    FaultEnergyConfig config;
    config.enabled = RequireBool(GetField(value, filename, "enabled"),
                                 filename,
                                 "energy.enabled");
    config.initialDod = RequireFiniteNumber(
        GetField(value, filename, "initial_dod"), filename, "initial_dod");
    config.riskDod = RequireFiniteNumber(GetField(value, filename, "risk_dod"),
                                         filename,
                                         "risk_dod");
    config.criticalDod = RequireFiniteNumber(
        GetField(value, filename, "critical_dod"), filename, "critical_dod");
    config.batteryWh = RequireFiniteNumber(GetField(value, filename, "battery_wh"),
                                           filename,
                                           "battery_wh");
    config.incrementalComputePowerW = RequireFiniteNumber(
        GetField(value, filename, "incremental_compute_power_w"),
        filename,
        "incremental_compute_power_w");
    config.correctionWeight = RequireFiniteNumber(
        GetField(value, filename, "correction_weight"), filename, "correction_weight");
    RequireRange(config.initialDod, 0.0, 1.0, filename, "initial_dod");
    RequireRange(config.riskDod, 0.0, 1.0, filename, "risk_dod");
    RequireRange(config.criticalDod, 0.0, 1.0, filename, "critical_dod");
    RequireRange(config.correctionWeight, 0.0, 1.0, filename, "correction_weight");
    if (!(config.initialDod <= config.riskDod &&
          config.riskDod < config.criticalDod))
    {
        Fail(filename,
             "self_state.energy",
             "must satisfy initial_dod <= risk_dod < critical_dod");
    }
    if (config.batteryWh <= 0.0 || config.incrementalComputePowerW < 0.0)
    {
        Fail(filename, "self_state.energy", "battery and compute power are invalid");
    }
    return config;
}

SelfStateFaultConfig
ParseSelfState(const Json& value, const std::filesystem::path& filename)
{
    RequireObjectFields(value,
                        filename,
                        "self_state",
                        {"enabled",
                         "temperature",
                         "energy",
                         "risk_threshold",
                         "max_failure_intensity_per_s"});
    SelfStateFaultConfig config;
    config.enabled = RequireBool(GetField(value, filename, "enabled"),
                                 filename,
                                 "self_state.enabled");
    config.temperature = ParseTemperature(GetField(value, filename, "temperature"),
                                          filename);
    config.energy = ParseEnergy(GetField(value, filename, "energy"), filename);
    config.riskThreshold = RequireFiniteNumber(
        GetField(value, filename, "risk_threshold"), filename, "risk_threshold");
    config.maxFailureIntensityPerSecond = RequireFiniteNumber(
        GetField(value, filename, "max_failure_intensity_per_s"),
        filename,
        "max_failure_intensity_per_s");
    RequireRange(config.riskThreshold, 0.0, 1.0, filename, "risk_threshold");
    if (config.maxFailureIntensityPerSecond < 0.0)
    {
        Fail(filename, "max_failure_intensity_per_s", "must be non-negative");
    }
    return config;
}

RadiationFaultConfig
ParseRadiation(const Json& value, const std::filesystem::path& filename)
{
    RequireObjectFields(value,
                        filename,
                        "radiation",
                        {"enabled",
                         "longitude_min_deg",
                         "longitude_max_deg",
                         "latitude_min_deg",
                         "latitude_max_deg",
                         "effective_failure_intensity_per_s",
                         "risk_threshold",
                         "reset_exposure_on_exit"});
    RadiationFaultConfig config;
    config.enabled = RequireBool(GetField(value, filename, "enabled"),
                                 filename,
                                 "radiation.enabled");
    config.longitudeMinDegrees = RequireFiniteNumber(
        GetField(value, filename, "longitude_min_deg"), filename, "longitude_min_deg");
    config.longitudeMaxDegrees = RequireFiniteNumber(
        GetField(value, filename, "longitude_max_deg"), filename, "longitude_max_deg");
    config.latitudeMinDegrees = RequireFiniteNumber(
        GetField(value, filename, "latitude_min_deg"), filename, "latitude_min_deg");
    config.latitudeMaxDegrees = RequireFiniteNumber(
        GetField(value, filename, "latitude_max_deg"), filename, "latitude_max_deg");
    config.effectiveFailureIntensityPerSecond = RequireFiniteNumber(
        GetField(value, filename, "effective_failure_intensity_per_s"),
        filename,
        "effective_failure_intensity_per_s");
    config.riskThreshold = RequireFiniteNumber(
        GetField(value, filename, "risk_threshold"), filename, "radiation.risk_threshold");
    config.resetExposureOnExit = RequireBool(
        GetField(value, filename, "reset_exposure_on_exit"),
        filename,
        "reset_exposure_on_exit");
    RequireRange(config.longitudeMinDegrees, -180.0, 180.0, filename, "longitude_min_deg");
    RequireRange(config.longitudeMaxDegrees, -180.0, 180.0, filename, "longitude_max_deg");
    RequireRange(config.latitudeMinDegrees, -90.0, 90.0, filename, "latitude_min_deg");
    RequireRange(config.latitudeMaxDegrees, -90.0, 90.0, filename, "latitude_max_deg");
    RequireRange(config.riskThreshold, 0.0, 1.0, filename, "radiation.risk_threshold");
    if (config.longitudeMinDegrees >= config.longitudeMaxDegrees ||
        config.latitudeMinDegrees >= config.latitudeMaxDegrees)
    {
        Fail(filename, "radiation", "region minimums must be smaller than maximums");
    }
    if (config.effectiveFailureIntensityPerSecond < 0.0)
    {
        Fail(filename, "effective_failure_intensity_per_s", "must be non-negative");
    }
    if (!config.resetExposureOnExit)
    {
        Fail(filename, "reset_exposure_on_exit", "must be true in N4B");
    }
    return config;
}

DebrisFaultConfig
ParseDebris(const Json& value, const std::filesystem::path& filename)
{
    RequireObjectFields(value,
                        filename,
                        "debris",
                        {"enabled",
                         "mode",
                         "fixed_count",
                         "single_satellite_intensity_per_s"});
    DebrisFaultConfig config;
    config.enabled = RequireBool(GetField(value, filename, "enabled"),
                                 filename,
                                 "debris.enabled");
    const Json& mode = GetField(value, filename, "mode");
    if (!mode.is_string())
    {
        Fail(filename, "debris.mode", "must be fixed_k or poisson");
    }
    config.mode = mode.get<std::string>();
    if (config.mode != "fixed_k" && config.mode != "poisson")
    {
        Fail(filename, "debris.mode", "must be fixed_k or poisson");
    }
    config.fixedCount = RequireUint32(GetField(value, filename, "fixed_count"),
                                      filename,
                                      "fixed_count");
    config.singleSatelliteIntensityPerSecond = RequireFiniteNumber(
        GetField(value, filename, "single_satellite_intensity_per_s"),
        filename,
        "single_satellite_intensity_per_s");
    if (config.singleSatelliteIntensityPerSecond < 0.0)
    {
        Fail(filename, "single_satellite_intensity_per_s", "must be non-negative");
    }
    if (config.mode == "fixed_k" && config.singleSatelliteIntensityPerSecond != 0.0)
    {
        Fail(filename, "debris", "fixed_k cannot also define a Poisson intensity");
    }
    if (config.mode == "poisson" && config.fixedCount != 0)
    {
        Fail(filename, "debris", "poisson cannot also define fixed_count");
    }
    if (config.enabled &&
        ((config.mode == "fixed_k" && config.fixedCount == 0) ||
         (config.mode == "poisson" &&
          config.singleSatelliteIntensityPerSecond == 0.0)))
    {
        Fail(filename, "debris", "enabled mode must define a positive event rate or count");
    }
    return config;
}

} // namespace

FaultModelConfig
ReadFaultModelConfig(const std::filesystem::path& filename)
{
    if (filename.empty())
    {
        throw FaultModelConfigError("fault model config path must not be empty");
    }
    const std::filesystem::path sourcePath =
        std::filesystem::absolute(filename).lexically_normal();
    const Json root = ReadJson(sourcePath);
    RequireObjectFields(root,
                        sourcePath,
                        "root",
                        {"schema_version",
                         "check_interval_ns",
                         "recoverable_compute_duration_ns",
                         "self_state",
                         "radiation",
                         "debris"});

    FaultModelConfig config;
    config.schemaVersion = RequireUint32(
        GetField(root, sourcePath, "schema_version"), sourcePath, "schema_version");
    if (config.schemaVersion != 1)
    {
        Fail(sourcePath, "schema_version", "must equal 1");
    }
    config.checkIntervalNs = RequirePositiveInt64(
        GetField(root, sourcePath, "check_interval_ns"),
        sourcePath,
        "check_interval_ns");
    config.recoverableComputeDurationNs = RequirePositiveInt64(
        GetField(root, sourcePath, "recoverable_compute_duration_ns"),
        sourcePath,
        "recoverable_compute_duration_ns");
    config.selfState = ParseSelfState(GetField(root, sourcePath, "self_state"),
                                      sourcePath);
    config.radiation = ParseRadiation(GetField(root, sourcePath, "radiation"),
                                      sourcePath);
    config.debris = ParseDebris(GetField(root, sourcePath, "debris"), sourcePath);
    return config;
}

} // namespace ns3
