/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_MODEL_CONFIG_H
#define SATCOMPUTE_FAULT_MODEL_CONFIG_H

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ns3
{

/** Strict Fault Model configuration parsing error. */
class FaultModelConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** F1 exponential thermal-state and temperature-risk parameters. */
struct FaultTemperatureConfig
{
    double baseC{}; ///< Idle equilibrium temperature in degrees Celsius.
    double saturationC{}; ///< Busy equilibrium temperature in degrees Celsius.
    double riskC{}; ///< Thermal-risk curve start temperature.
    double criticalC{}; ///< Deterministic protection-shutdown temperature.
    double heatingTauSeconds{}; ///< Busy exponential time constant.
    double coolingTauSeconds{}; ///< Idle exponential time constant.
    double growthFactor{}; ///< Thermal-risk exponential shape parameter.
};

/** F1 compute-energy correction parameters. */
struct FaultEnergyConfig
{
    bool enabled{}; ///< Whether energy pressure contributes to F1 risk.
    double initialDod{}; ///< Initial normalized depth of discharge.
    double riskDod{}; ///< Energy-pressure start depth of discharge.
    double criticalDod{}; ///< Energy-pressure normalization upper bound.
    double batteryWh{}; ///< Battery energy capacity in watt-hours.
    double incrementalComputePowerW{}; ///< Busy incremental power in watts.
    double correctionWeight{}; ///< Energy-pressure contribution in [0, 1].
};

/** Complete F1 self-state fault-model configuration. */
struct SelfStateFaultConfig
{
    bool enabled{}; ///< Whether online F1 generation is enabled.
    FaultTemperatureConfig temperature; ///< Thermal parameters.
    FaultEnergyConfig energy; ///< Optional energy correction.
    double riskThreshold{}; ///< Combined-risk notice threshold.
    double maxFailureIntensityPerSecond{}; ///< Maximum F1 hazard intensity.
};

/** F2 geographic exposure and effective-intensity configuration. */
struct RadiationFaultConfig
{
    bool enabled{}; ///< Whether online F2 generation is enabled.
    double longitudeMinDegrees{}; ///< Inclusive western longitude boundary.
    double longitudeMaxDegrees{}; ///< Inclusive eastern longitude boundary.
    double latitudeMinDegrees{}; ///< Inclusive southern latitude boundary.
    double latitudeMaxDegrees{}; ///< Inclusive northern latitude boundary.
    double effectiveFailureIntensityPerSecond{}; ///< Region-only F2 hazard intensity.
    double riskThreshold{}; ///< Cumulative-exposure notice threshold.
    bool resetExposureOnExit{}; ///< Whether region exit closes and resets exposure.
};

/** F3 permanent-satellite-fault generation configuration. */
struct DebrisFaultConfig
{
    bool enabled{}; ///< Whether online F3 generation is enabled.
    std::string mode; ///< fixed_k or poisson generation mode.
    uint32_t fixedCount{}; ///< Permanent faults in fixed_k mode.
    double singleSatelliteIntensityPerSecond{}; ///< Per-alive-satellite Poisson intensity.
};

/** Strict unified N4B model configuration. */
struct FaultModelConfig
{
    uint32_t schemaVersion{}; ///< Configuration schema, currently one.
    int64_t checkIntervalNs{}; ///< F1/F2 update and compute-sampling interval.
    int64_t recoverableComputeDurationNs{}; ///< F1/F2 compute outage duration.
    SelfStateFaultConfig selfState; ///< F1 parameters.
    RadiationFaultConfig radiation; ///< F2 parameters.
    DebrisFaultConfig debris; ///< F3 parameters.
};

/**
 * Read and strictly validate one unified N4B fault-model configuration.
 *
 * @param filename JSON input path.
 * @return Typed closed-world configuration.
 */
FaultModelConfig ReadFaultModelConfig(const std::filesystem::path& filename);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_MODEL_CONFIG_H
