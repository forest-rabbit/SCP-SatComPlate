/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-parameter-validator.h"

#include <cmath>
#include <string_view>

namespace ns3
{

namespace
{

[[noreturn]] void
Fail(std::string_view field, std::string_view message)
{
    throw FaultParameterError(std::string(field) + " " + std::string(message));
}

void
RequireFinite(double value, std::string_view field)
{
    if (!std::isfinite(value))
    {
        Fail(field, "must be finite");
    }
}

void
RequireRange(double value, double minimum, double maximum, std::string_view field)
{
    RequireFinite(value, field);
    if (value < minimum || value > maximum)
    {
        Fail(field, "is outside the permitted range");
    }
}

} // namespace

void
ValidateFaultParameters(const FaultParameters& parameters)
{
    RequireFinite(parameters.checkIntervalSeconds, "common.check_interval_s");
    RequireFinite(parameters.recoverableComputeDurationSeconds,
                  "common.recoverable_compute_duration_s");
    if (parameters.checkIntervalSeconds <= 0.0 ||
        parameters.recoverableComputeDurationSeconds <= 0.0)
    {
        Fail("common", "time parameters must be positive");
    }

    const F1TemperatureParameters& temperature = parameters.f1.temperature;
    RequireFinite(temperature.baseC, "F1.temperature.base_c");
    RequireFinite(temperature.saturationC, "F1.temperature.saturation_c");
    RequireFinite(temperature.riskC, "F1.temperature.risk_c");
    RequireFinite(temperature.criticalC, "F1.temperature.critical_c");
    RequireFinite(temperature.heatingTauSeconds, "F1.temperature.heating_tau_s");
    RequireFinite(temperature.coolingTauSeconds, "F1.temperature.cooling_tau_s");
    RequireFinite(temperature.growthFactor, "F1.temperature.growth_factor");
    if (!(temperature.baseC < temperature.riskC &&
          temperature.riskC < temperature.criticalC &&
          temperature.criticalC < temperature.saturationC))
    {
        Fail("F1.temperature",
             "must satisfy base_c < risk_c < critical_c < saturation_c");
    }
    if (temperature.heatingTauSeconds <= 0.0 ||
        temperature.coolingTauSeconds <= 0.0 || temperature.growthFactor <= 0.0)
    {
        Fail("F1.temperature", "time constants and growth must be positive");
    }

    const F1EnergyParameters& energy = parameters.f1.energy;
    RequireRange(energy.initialDod, 0.0, 1.0, "F1.energy.initial_dod");
    RequireRange(energy.riskDod, 0.0, 1.0, "F1.energy.risk_dod");
    RequireRange(energy.criticalDod, 0.0, 1.0, "F1.energy.critical_dod");
    RequireRange(energy.correctionWeight, 0.0, 1.0, "F1.energy.correction_weight");
    RequireFinite(energy.batteryWh, "F1.energy.battery_wh");
    RequireFinite(energy.incrementalComputePowerW,
                  "F1.energy.incremental_compute_power_w");
    if (!(energy.initialDod <= energy.riskDod && energy.riskDod < energy.criticalDod))
    {
        Fail("F1.energy", "must satisfy initial_dod <= risk_dod < critical_dod");
    }
    if (energy.batteryWh <= 0.0 || energy.incrementalComputePowerW < 0.0)
    {
        Fail("F1.energy", "battery and compute power are invalid");
    }
    RequireRange(parameters.f1.riskThreshold, 0.0, 1.0, "F1.risk_threshold");
    RequireFinite(parameters.f1.maxFailureIntensityPerSecond,
                  "F1.max_failure_intensity_per_s");
    if (parameters.f1.maxFailureIntensityPerSecond < 0.0)
    {
        Fail("F1.max_failure_intensity_per_s", "must be non-negative");
    }

    const F2FaultParameters& f2 = parameters.f2;
    RequireRange(f2.longitudeMinDegrees, -180.0, 180.0, "F2.longitude_min_deg");
    RequireRange(f2.longitudeMaxDegrees, -180.0, 180.0, "F2.longitude_max_deg");
    RequireRange(f2.latitudeMinDegrees, -90.0, 90.0, "F2.latitude_min_deg");
    RequireRange(f2.latitudeMaxDegrees, -90.0, 90.0, "F2.latitude_max_deg");
    RequireRange(f2.hotspotLongitudeDegrees,
                 -180.0,
                 180.0,
                 "F2.hotspot_longitude_deg");
    RequireRange(f2.hotspotLatitudeDegrees,
                 -90.0,
                 90.0,
                 "F2.hotspot_latitude_deg");
    RequireFinite(f2.sigmaLongitudeWestDegrees,
                  "F2.sigma_longitude_west_deg");
    RequireFinite(f2.sigmaLongitudeEastDegrees,
                  "F2.sigma_longitude_east_deg");
    RequireFinite(f2.sigmaLatitudeDegrees, "F2.sigma_latitude_deg");
    RequireRange(f2.spatialRiskThreshold,
                 0.0,
                 1.0,
                 "F2.spatial_risk_threshold");
    RequireFinite(f2.referenceSeuIntensityPerSecond,
                  "F2.reference_seu_intensity_per_s");
    RequireRange(f2.seuToComputeFailureProbability,
                 0.0,
                 1.0,
                 "F2.seu_to_compute_failure_probability");
    if (f2.longitudeMinDegrees >= f2.longitudeMaxDegrees ||
        f2.latitudeMinDegrees >= f2.latitudeMaxDegrees)
    {
        Fail("F2.region", "minimums must be smaller than maximums");
    }
    if (f2.hotspotLongitudeDegrees < f2.longitudeMinDegrees ||
        f2.hotspotLongitudeDegrees > f2.longitudeMaxDegrees ||
        f2.hotspotLatitudeDegrees < f2.latitudeMinDegrees ||
        f2.hotspotLatitudeDegrees > f2.latitudeMaxDegrees)
    {
        Fail("F2.hotspot", "must be inside the configured radiation region");
    }
    if (f2.sigmaLongitudeWestDegrees <= 0.0 ||
        f2.sigmaLongitudeEastDegrees <= 0.0 || f2.sigmaLatitudeDegrees <= 0.0)
    {
        Fail("F2.sigma", "values must be positive");
    }
    if (f2.sigmaLongitudeWestDegrees >= f2.sigmaLongitudeEastDegrees)
    {
        Fail("F2.sigma_longitude",
             "must satisfy west sigma < east sigma");
    }
    if (f2.spatialRiskThreshold <= 0.0 || f2.spatialRiskThreshold >= 1.0)
    {
        Fail("F2.spatial_risk_threshold", "must be strictly between zero and one");
    }
    if (f2.referenceSeuIntensityPerSecond < 0.0)
    {
        Fail("F2.reference_seu_intensity_per_s", "must be non-negative");
    }

    const F3FaultParameters& f3 = parameters.f3;
    RequireFinite(f3.singleSatelliteIntensityPerSecond,
                  "F3.single_satellite_intensity_per_s");
    if (f3.mode != "fixed_k" && f3.mode != "poisson" && f3.mode != "controlled")
    {
        Fail("F3.mode", "must be fixed_k, poisson or controlled");
    }
    RequireFinite(f3.controlledStartSeconds, "F3.controlled_start_s");
    if (f3.controlledStartSeconds < 0.0 ||
        (f3.mode == "controlled" &&
         (f3.fixedCount != 1 || f3.singleSatelliteIntensityPerSecond != 0.0)))
    {
        Fail("F3.controlled", "requires non-negative time, fixed_count=1 and no Poisson intensity");
    }
    if (f3.singleSatelliteIntensityPerSecond < 0.0)
    {
        Fail("F3.single_satellite_intensity_per_s", "must be non-negative");
    }
    if (f3.mode == "fixed_k" && f3.singleSatelliteIntensityPerSecond != 0.0)
    {
        Fail("F3", "fixed_k cannot also define a Poisson intensity");
    }
    if (f3.mode == "poisson" && f3.fixedCount != 0)
    {
        Fail("F3", "poisson cannot also define fixed_count");
    }
    if (f3.enabled &&
        ((f3.mode == "fixed_k" && f3.fixedCount == 0) ||
         (f3.mode == "poisson" && f3.singleSatelliteIntensityPerSecond == 0.0)))
    {
        Fail("F3", "enabled mode must define a positive event rate or count");
    }
}

} // namespace ns3
