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

class FaultModelConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct FaultTemperatureConfig
{
    double baseC{};
    double saturationC{};
    double riskC{};
    double criticalC{};
    double heatingTauSeconds{};
    double coolingTauSeconds{};
    double growthFactor{};
};

struct FaultEnergyConfig
{
    bool enabled{};
    double initialDod{};
    double riskDod{};
    double criticalDod{};
    double batteryWh{};
    double incrementalComputePowerW{};
    double correctionWeight{};
};

struct SelfStateFaultConfig
{
    bool enabled{};
    FaultTemperatureConfig temperature;
    FaultEnergyConfig energy;
    double riskThreshold{};
    double maxFailureIntensityPerSecond{};
};

struct RadiationFaultConfig
{
    bool enabled{};
    double longitudeMinDegrees{};
    double longitudeMaxDegrees{};
    double latitudeMinDegrees{};
    double latitudeMaxDegrees{};
    double effectiveFailureIntensityPerSecond{};
    double riskThreshold{};
    bool resetExposureOnExit{};
};

struct DebrisFaultConfig
{
    bool enabled{};
    std::string mode;
    uint32_t fixedCount{};
    double singleSatelliteIntensityPerSecond{};
};

struct FaultModelConfig
{
    uint32_t schemaVersion{};
    int64_t checkIntervalNs{};
    int64_t recoverableComputeDurationNs{};
    SelfStateFaultConfig selfState;
    RadiationFaultConfig radiation;
    DebrisFaultConfig debris;
};

/** Read and strictly validate one unified N4B fault-model configuration. */
FaultModelConfig ReadFaultModelConfig(const std::filesystem::path& filename);

} // namespace ns3

#endif // SATCOMPUTE_FAULT_MODEL_CONFIG_H
