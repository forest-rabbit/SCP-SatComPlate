/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/compute-failure-predictor.h"
#include "ns3/compute-fault-combination.h"
#include "ns3/fault-parameter-validator.h"
#include "ns3/fault-para.h"
#include "ns3/f1-self-state-fault-model.h"
#include "ns3/f2-radiation-fault-model.h"
#include "ns3/f3-debris-fault-model.h"
#include "ns3/geographic-positions.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

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
              parameters.f2.recoveryDurationSeconds == 8.0,
          "fault common defaults differ");
    Check(parameters.f1.enabled && parameters.f1.temperature.baseC == 17.0 &&
              parameters.f1.temperature.heatingToCriticalSeconds == 30.0 &&
              parameters.f1.temperature.heatingShapeGamma == 1.5 &&
              parameters.f1.temperature.coolingFromCriticalToBaseSeconds == 4.0 &&
              parameters.f1.energy.initialDod == 0.25 &&
              parameters.f1.temperature.growthFactor == 10.0,
          "F1 defaults differ");
    Check(!parameters.f2.enabled &&
              parameters.f2.longitudeMinDegrees == -90.0 &&
              parameters.f2.hotspotLongitudeDegrees == -60.0 &&
              parameters.f2.hotspotLatitudeDegrees == -28.0 &&
              parameters.f2.sigmaLongitudeWestDegrees == 12.0 &&
              parameters.f2.sigmaLongitudeEastDegrees == 24.0 &&
              parameters.f2.sigmaLatitudeDegrees == 12.0 &&
              parameters.f2.spatialRiskThreshold == 0.5 &&
              parameters.f2.referenceSeuIntensityPerSecond ==
                  0.002859196111093899 &&
              parameters.f2.seuToComputeFailureProbability == 0.5 &&
              !parameters.f3.enabled &&
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
    value.f2.recoveryDurationSeconds =
        std::numeric_limits<double>::infinity();
    ExpectError(value, "F2.recovery", "non-finite-recovery");

    value = GetDefaultFaultParameters();
    value.f1.temperature.riskC = 31.0;
    ExpectError(value, "F1.temperature", "temperature-order");

    value = GetDefaultFaultParameters();
    value.f1.temperature.heatingToCriticalSeconds = 0.0;
    ExpectError(value, "F1.temperature", "heating-tau");

    value = GetDefaultFaultParameters();
    value.f1.temperature.heatingToCriticalSeconds =
        std::numeric_limits<double>::infinity();
    ExpectError(value, "F1.temperature.heating_to_critical_s", "non-finite-heating-tau");

    for (double gamma : {0.5, std::numeric_limits<double>::infinity()})
    {
        value = GetDefaultFaultParameters();
        value.f1.temperature.heatingShapeGamma = gamma;
        ExpectError(value, "F1.temperature.heating_shape_gamma", "invalid-heating-shape");
    }

    value = GetDefaultFaultParameters();
    value.f1.energy.initialDod = 0.4;
    ExpectError(value, "F1.energy", "dod-order");

    value = GetDefaultFaultParameters();
    value.f2.longitudeMinDegrees = 10.0;
    ExpectError(value, "F2.region", "F2-region");

    value = GetDefaultFaultParameters();
    value.f2.hotspotLongitudeDegrees = 20.0;
    ExpectError(value, "F2.hotspot", "F2-hotspot");

    value = GetDefaultFaultParameters();
    value.f2.sigmaLatitudeDegrees = 0.0;
    ExpectError(value, "F2.sigma", "F2-sigma");

    value = GetDefaultFaultParameters();
    value.f2.sigmaLongitudeWestDegrees =
        value.f2.sigmaLongitudeEastDegrees;
    ExpectError(value, "F2.sigma_longitude", "F2-longitude-sigma-order");

    value = GetDefaultFaultParameters();
    value.f2.spatialRiskThreshold = 1.0;
    ExpectError(value, "F2.spatial_risk_threshold", "F2-spatial-threshold");

    value = GetDefaultFaultParameters();
    value.f2.referenceSeuIntensityPerSecond = -0.1;
    ExpectError(value, "F2.reference_seu_intensity_per_s", "negative-SEU-intensity");

    value = GetDefaultFaultParameters();
    value.f2.seuToComputeFailureProbability = 1.1;
    ExpectError(value,
                "F2.seu_to_compute_failure_probability",
                "invalid-SEU-failure-mapping");

    value = GetDefaultFaultParameters();
    value.f2.seuToComputeFailureProbability = -0.1;
    ExpectError(value,
                "F2.seu_to_compute_failure_probability",
                "negative-SEU-failure-mapping");

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
    auto parameters = GetDefaultFaultParameters();
    parameters.f1.temperature.heatingShapeGamma = 1.0;
    const F1SelfStateFaultModel model(parameters.f1);
    Check(std::abs(model.GetHeatingCoefficient() - std::log(18. / 5.) / 30.) < 1e-14,
          "exponential coefficient must derive from the 30-second target");
    Check(model.GetCoolingRate() == 3.25, "linear cooling rate differs");
    const std::array<double, 7> temperatures = {17, 20.460300806993335, 23.25539707649341,
        25.513167019494862, 27.336905676064468, 28.81005075790781, 30};
    auto state = model.CreateInitialSnapshot();
    double previousRise = 100;
    for (unsigned i = 1; i < temperatures.size(); ++i)
    {
        const double previous = state.temperatureC;
        model.Update(state, true, 5);
        Check(std::abs(state.temperatureC - temperatures[i]) < 1e-11, "heating reference differs");
        const double rise = state.temperatureC - previous;
        Check(rise > 0 && rise < previousRise, "heating must be fast then slow");
        previousRise = rise;
    }
    Check(state.stepFailureProbability > 1 - 1e-12, "critical probability must be one");
    Check(std::abs(model.GetRecoveryDurationSeconds(state.temperatureC) - 4) < 1e-12,
          "critical recovery duration differs");
    Check(model.GetRecoveryDurationSeconds(31) == 4, "protected overshoot recovery exceeds four seconds");
    model.Update(state, false, 4);
    Check(state.temperatureC == 17 && state.stepFailureProbability == 0,
          "critical cooling must reach base naturally");
    state.temperatureC = 25;
    const double duration = model.GetRecoveryDurationSeconds(25);
    Check(duration > 2.46 && duration < 2.47, "dynamic recovery differs");
    model.Update(state, false, duration / 2);
    model.Update(state, false, duration / 2);
    Check(std::abs(state.temperatureC - 17) < 1e-12, "fractional cooling differs");

    for (double beta : {3., 4., 5., 6., 8., 10.})
    {
        parameters.f1.temperature.growthFactor = beta;
        const F1SelfStateFaultModel candidate(parameters.f1);
        double previous = -1;
        for (int k = 0; k <= 100; ++k)
        {
            auto sample = candidate.CreateInitialSnapshot();
            sample.temperatureC = 20 + k / 10.;
            candidate.Evaluate(sample);
            const double expected = std::expm1(beta * k / 100.) / std::expm1(beta);
            Check(std::abs(sample.stepFailureProbability - expected) < 1e-13,
                  "temperature one-second probability differs");
            Check(sample.stepFailureProbability >= previous &&
                  sample.stepFailureProbability >= 0 && sample.stepFailureProbability <= 1,
                  "probability must be monotonic and bounded");
            previous = sample.stepFailureProbability;
        }
    }
    auto energy = model.CreateInitialSnapshot();
    auto cadence = model.CreateInitialSnapshot();
    cadence.temperatureC = 25;
    model.Evaluate(cadence);
    const double oneSecondProbability = cadence.stepFailureProbability;
    for (double interval : {.25, 1., 2.})
    {
        model.Evaluate(cadence, interval);
        Check(std::abs(cadence.stepFailureProbability -
                       (-std::expm1(std::log1p(-oneSecondProbability) * interval))) < 1e-14 &&
                  cadence.temperatureC == 25,
              "sampling cadence must rescale the one-second probability without heating");
    }
    energy.depthOfDischarge = parameters.f1.energy.criticalDod;
    for (double temperature : {17., 20., 25., 29., 30.})
    {
        energy.temperatureC = temperature;
        model.Evaluate(energy);
        Check(std::abs(energy.combinedRisk - std::min(1., energy.thermalRisk * 1.1)) < 1e-14,
              "energy must only multiply thermal probability");
        if (temperature <= 20)
            Check(energy.stepFailureProbability == 0, "energy-only low-temperature fault");
    }
    const double dod = energy.depthOfDischarge;
    model.Update(energy, false, 4);
    Check(energy.depthOfDischarge == dod, "cooling reset accumulated energy");

    auto exact = model.CreateInitialSnapshot();
    int64_t last = 0;
    model.AdvanceTo(exact, last, 750000000, true);
    model.AdvanceTo(exact, last, 2250000000, false);
    auto reference = model.CreateInitialSnapshot();
    model.Update(reference, true, 1.5);
    Check(std::abs(exact.temperatureC - reference.temperatureC) < 1e-12 &&
          std::abs(exact.depthOfDischarge - reference.depthOfDischarge) < 1e-15,
          "transition used new busy state for elapsed interval");
    model.AdvanceTo(exact, last, 2500000000, true);
    model.Update(reference, false, .25);
    Check(std::abs(exact.temperatureC - reference.temperatureC) < 1e-12,
          "fractional idle interval missing");
    model.AdvanceTo(exact, last, 3500000000, true);
    model.Update(reference, true, 1);
    Check(std::abs(exact.stepFailureProbability - reference.stepFailureProbability) < 1e-14,
          "elapsed-time bookkeeping changed the probability interval");

    for (double gamma : {1.0 + 1e-8, 1.5, 2.0})
    {
        parameters.f1.temperature.heatingShapeGamma = gamma;
        const F1SelfStateFaultModel shaped(parameters.f1);
        auto current = shaped.CreateInitialSnapshot();
        auto exponential = model.CreateInitialSnapshot();
        double lastRise = 100;
        for (unsigned i = 1; i <= 120; ++i)
        {
            const double old = current.temperatureC;
            shaped.Update(current, true, .25);
            model.Update(exponential, true, .25);
            const double rise = current.temperatureC - old;
            Check(rise > 0 && rise < lastRise && current.temperatureC < 35,
                  "shaped heating must increase monotonically with decreasing slope");
            lastRise = rise;
            if (i < 120)
                Check(current.temperatureC > exponential.temperatureC,
                      "gamma above one must heat faster before the shared critical endpoint");
        }
        Check(std::abs(current.temperatureC - 30) < 1e-10,
              "shaped heating missed the 30-second endpoint");
        shaped.Update(current, false, 4);
        Check(std::abs(current.temperatureC - 17) < 1e-10,
              "gamma must not change linear cooling");
        for (double initial : {17., 23.125, 29.75, 35.})
        {
            auto whole = shaped.CreateInitialSnapshot();
            whole.temperatureC = initial;
            auto split = whole;
            shaped.Update(whole, true, 7.85);
            shaped.Update(split, true, 4.125);
            shaped.Update(split, true, 3.725);
            Check(std::abs(whole.temperatureC - split.temperatureC) < 1e-11 &&
                      std::abs(whole.depthOfDischarge - split.depthOfDischarge) < 1e-14,
                  "fractional shaped flow depends on event partition or resets temperature");
        }
        auto asymptote = shaped.CreateInitialSnapshot();
        shaped.Update(asymptote, true, 1e6);
        Check(asymptote.temperatureC > 34.99 && asymptote.temperatureC <= 35,
              "shaped equilibrium must remain 35 degrees");
    }
}

Vector
MakeEcef(double latitudeDegrees, double longitudeDegrees)
{
    return GeographicPositions::GeographicToCartesianCoordinates(
        latitudeDegrees,
        longitudeDegrees,
        780000.0,
        GeographicPositions::SPHERE);
}

void
CheckF2Model()
{
    FaultParameters parameters = GetDefaultFaultParameters();
    parameters.f2.referenceSeuIntensityPerSecond = 0.02;
    parameters.f2.seuToComputeFailureProbability = 0.5;
    parameters.f2.spatialRiskThreshold = 0.5;
    const F2RadiationFaultModel model(parameters.f2);
    Check(std::abs(model.GetMaximumFailureIntensityPerSecond() - 0.01) < 1e-15,
          "F2 maximum effective intensity differs");

    F2FaultParameters globalParameters = parameters.f2;
    globalParameters.longitudeMinDegrees = -180.0;
    globalParameters.longitudeMaxDegrees = 180.0;
    globalParameters.latitudeMinDegrees = -90.0;
    globalParameters.latitudeMaxDegrees = 90.0;
    const F2RadiationFaultModel globalModel(globalParameters);
    const double radius = GeographicPositions::EARTH_SPHERE_RADIUS;
    const std::array<std::pair<Vector, std::pair<double, double>>, 4> axes = {{
        {Vector(radius, 0.0, 0.0), {0.0, 0.0}},
        {Vector(0.0, radius, 0.0), {0.0, 90.0}},
        {Vector(-radius, 0.0, 0.0), {0.0, -180.0}},
        {Vector(0.0, 0.0, radius), {90.0, 0.0}},
    }};
    for (const auto& [ecef, expected] : axes)
    {
        F2RadiationFaultSnapshot snapshot = globalModel.CreateInitialSnapshot();
        globalModel.Update(snapshot, ecef, 0.0);
        Check(std::abs(snapshot.latitudeDegrees - expected.first) < 1e-9 &&
                  std::abs(snapshot.longitudeDegrees - expected.second) < 1e-9,
              "F2 ECEF axis conversion differs");
    }

    const std::array<Vector, 4> boundaries = {
        MakeEcef(0.0, parameters.f2.longitudeMinDegrees),
        MakeEcef(0.0, parameters.f2.longitudeMaxDegrees),
        MakeEcef(parameters.f2.latitudeMinDegrees, 0.0),
        MakeEcef(parameters.f2.latitudeMaxDegrees, 0.0),
    };
    for (const Vector& boundary : boundaries)
    {
        F2RadiationFaultSnapshot snapshot = model.CreateInitialSnapshot();
        model.Update(snapshot, boundary, 0.0);
        Check(snapshot.inRegion, "F2 rectangle rejected an inclusive boundary");
    }

    constexpr double epsilon = 0.001;
    const std::array<Vector, 4> outside = {
        MakeEcef(0.0, parameters.f2.longitudeMinDegrees - epsilon),
        MakeEcef(0.0, parameters.f2.longitudeMaxDegrees + epsilon),
        MakeEcef(parameters.f2.latitudeMinDegrees - epsilon, 0.0),
        MakeEcef(parameters.f2.latitudeMaxDegrees + epsilon, 0.0),
    };
    for (const Vector& position : outside)
    {
        F2RadiationFaultSnapshot snapshot = model.CreateInitialSnapshot();
        model.Update(snapshot, position, 0.0);
        Check(!snapshot.inRegion, "F2 rectangle accepted an outside position");
    }

    const std::array<double, 6> sampleLongitudes = {-90.0,
                                                    -75.0,
                                                    -60.0,
                                                    -40.0,
                                                    0.0,
                                                    5.0};
    const std::array<double, 5> sampleLatitudes = {-50.0,
                                                   -40.0,
                                                   -28.0,
                                                   -10.0,
                                                   5.0};
    for (const double longitude : sampleLongitudes)
    {
        for (const double latitude : sampleLatitudes)
        {
            F2RadiationFaultSnapshot snapshot = model.CreateInitialSnapshot();
            model.Update(snapshot, MakeEcef(latitude, longitude), 1.0);
            Check(std::isfinite(snapshot.spatialRisk) &&
                      snapshot.spatialRisk >= 0.0 && snapshot.spatialRisk <= 1.0 &&
                      std::isfinite(snapshot.stepFailureProbability) &&
                      snapshot.stepFailureProbability >= 0.0 &&
                      snapshot.stepFailureProbability <= 1.0,
                  "F2 spatial field produced a non-finite or invalid probability");
        }
    }

    F2RadiationFaultSnapshot hotspot = model.CreateInitialSnapshot();
    const Vector hotspotPosition =
        MakeEcef(parameters.f2.hotspotLatitudeDegrees,
                 parameters.f2.hotspotLongitudeDegrees);
    model.Update(hotspot, hotspotPosition, 0.0);
    Check(hotspot.inRegion && std::abs(hotspot.spatialRisk - 1.0) < 1e-15 &&
              std::abs(hotspot.seuIntensityPerSecond - 0.02) < 1e-15 &&
              std::abs(hotspot.failureIntensityPerSecond - 0.01) < 1e-15 &&
              hotspot.stepFailureProbability == 0.0 && model.IsRiskActive(hotspot),
          "F2 hotspot did not produce unit spatial risk and active notice state");

    F2RadiationFaultSnapshot longitudeNear = model.CreateInitialSnapshot();
    F2RadiationFaultSnapshot longitudeFar = model.CreateInitialSnapshot();
    model.Update(longitudeNear,
                 MakeEcef(parameters.f2.hotspotLatitudeDegrees, -50.0),
                 1.0);
    model.Update(longitudeFar,
                 MakeEcef(parameters.f2.hotspotLatitudeDegrees, -30.0),
                 1.0);
    Check(longitudeNear.spatialRisk < hotspot.spatialRisk &&
              longitudeNear.spatialRisk > longitudeFar.spatialRisk,
          "F2 longitude risk is not monotonic away from the hotspot");

    F2RadiationFaultSnapshot westEqualDistance = model.CreateInitialSnapshot();
    F2RadiationFaultSnapshot eastEqualDistance = model.CreateInitialSnapshot();
    model.Update(westEqualDistance,
                 MakeEcef(parameters.f2.hotspotLatitudeDegrees, -70.0),
                 1.0);
    model.Update(eastEqualDistance,
                 MakeEcef(parameters.f2.hotspotLatitudeDegrees, -50.0),
                 1.0);
    Check(westEqualDistance.spatialRisk < eastEqualDistance.spatialRisk,
          "F2 longitude field does not contract west and extend east");

    F2RadiationFaultSnapshot latitudeNear = model.CreateInitialSnapshot();
    F2RadiationFaultSnapshot latitudeFar = model.CreateInitialSnapshot();
    model.Update(latitudeNear,
                 MakeEcef(-20.0, parameters.f2.hotspotLongitudeDegrees),
                 1.0);
    model.Update(latitudeFar,
                 MakeEcef(0.0, parameters.f2.hotspotLongitudeDegrees),
                 1.0);
    Check(latitudeNear.spatialRisk < hotspot.spatialRisk &&
              latitudeNear.spatialRisk > latitudeFar.spatialRisk,
          "F2 latitude risk is not monotonic away from the hotspot");

    F2RadiationFaultSnapshot noticeState = model.CreateInitialSnapshot();
    uint32_t noticeTransitions = 0;
    uint32_t noticeClearTransitions = 0;
    bool previousRiskActive = model.IsRiskActive(noticeState);
    const auto updateNoticeState = [&](const Vector& position) {
        model.Update(noticeState, position, 1.0);
        const bool currentRiskActive = model.IsRiskActive(noticeState);
        noticeTransitions += !previousRiskActive && currentRiskActive ? 1 : 0;
        noticeClearTransitions += previousRiskActive && !currentRiskActive ? 1 : 0;
        previousRiskActive = currentRiskActive;
    };
    updateNoticeState(MakeEcef(-50.0, -60.0));
    const double peripheralStepProbability = noticeState.stepFailureProbability;
    Check(!previousRiskActive && peripheralStepProbability > 0.0,
          "F2 periphery cannot represent an unannounced fault probability");
    updateNoticeState(MakeEcef(-35.0, -60.0));
    const double highRiskStepProbability = noticeState.stepFailureProbability;
    updateNoticeState(MakeEcef(-35.0, -60.0));
    updateNoticeState(MakeEcef(-50.0, -60.0));
    Check(noticeTransitions == 1 && noticeClearTransitions == 1 &&
              !previousRiskActive &&
              highRiskStepProbability > peripheralStepProbability,
          "F2 spatial NOTICE crossing semantics differ");

    F2RadiationFaultSnapshot northbound = model.CreateInitialSnapshot();
    F2RadiationFaultSnapshot southbound = model.CreateInitialSnapshot();
    model.Update(northbound, MakeEcef(-40.0, -60.0), 0.0);
    for (int second = 0; second < 10; ++second)
    {
        model.Update(northbound, MakeEcef(-40.0, -60.0), 1.0);
    }
    model.Update(northbound, hotspotPosition, 1.0);
    model.Update(southbound, MakeEcef(-10.0, -60.0), 0.0);
    model.Update(southbound, hotspotPosition, 1.0);
    Check(northbound.continuousExposureSeconds !=
                  southbound.continuousExposureSeconds &&
              northbound.spatialRisk == southbound.spatialRisk &&
              northbound.failureIntensityPerSecond ==
                  southbound.failureIntensityPerSecond &&
              northbound.stepFailureProbability ==
                  southbound.stepFailureProbability,
          "F2 current risk depends on direction or preceding exposure time");

    F2FaultParameters zeroMappingParameters = parameters.f2;
    zeroMappingParameters.seuToComputeFailureProbability = 0.0;
    const F2RadiationFaultModel zeroMappingModel(zeroMappingParameters);
    F2RadiationFaultSnapshot zeroMapping = zeroMappingModel.CreateInitialSnapshot();
    zeroMappingModel.Update(zeroMapping, hotspotPosition, 1.0);
    F2FaultParameters fullMappingParameters = parameters.f2;
    fullMappingParameters.seuToComputeFailureProbability = 1.0;
    const F2RadiationFaultModel fullMappingModel(fullMappingParameters);
    F2RadiationFaultSnapshot fullMapping = fullMappingModel.CreateInitialSnapshot();
    fullMappingModel.Update(fullMapping, hotspotPosition, 1.0);
    Check(zeroMapping.failureIntensityPerSecond == 0.0 &&
              zeroMapping.stepFailureProbability == 0.0 &&
              std::abs(fullMapping.failureIntensityPerSecond -
                       fullMapping.seuIntensityPerSecond) < 1e-15 &&
              fullMapping.stepFailureProbability >
                  northbound.stepFailureProbability,
          "F2 SEU-to-compute mapping boundaries differ");

    F2RadiationFaultSnapshot exposure = model.CreateInitialSnapshot();
    const Vector insidePosition = hotspotPosition;
    model.Update(exposure, insidePosition, 0.0);
    Check(exposure.inRegion && exposure.continuousExposureSeconds == 0.0 &&
              exposure.cumulativeFailureHazard == 0.0 &&
              exposure.cumulativeFailureProbability == 0.0 &&
              exposure.stepFailureProbability == 0.0,
          "F2 entry did not begin at zero exposure");

    const double expectedStepProbability = -std::expm1(-0.01);
    for (int second = 1; second <= 10; ++second)
    {
        model.Update(exposure, insidePosition, 1.0);
        Check(exposure.continuousExposureSeconds == second &&
                  std::abs(exposure.stepFailureProbability - expectedStepProbability) <
                      1e-15 &&
                  std::abs(exposure.cumulativeFailureProbability -
                           (-std::expm1(-0.01 * second))) < 1e-15,
              "F2 pass statistic or current-step probability differs");
    }
    Check(model.IsRiskActive(exposure), "F2 hotspot risk missed its threshold");
    Check(std::abs(std::pow(1.0 - expectedStepProbability, 10.0) -
                   std::exp(-0.01 * 10.0)) < 1e-15,
          "F2 repeated-step survival differs from the exponential process");

    model.Update(exposure, MakeEcef(25.0, 45.0), 1.0);
    Check(!exposure.inRegion && exposure.continuousExposureSeconds == 0.0 &&
              exposure.cumulativeFailureHazard == 0.0 &&
              exposure.cumulativeFailureProbability == 0.0 &&
              exposure.spatialRisk == 0.0 &&
              exposure.seuIntensityPerSecond == 0.0 &&
              exposure.failureIntensityPerSecond == 0.0 &&
              exposure.stepFailureProbability == 0.0 &&
              !model.IsRiskActive(exposure),
          "F2 exit did not clear continuous exposure and current probability");
    model.Update(exposure, insidePosition, 1.0);
    Check(exposure.inRegion && exposure.continuousExposureSeconds == 0.0 &&
              std::abs(exposure.cumulativeFailureHazard - 0.01) < 1e-15 &&
              std::abs(exposure.cumulativeFailureProbability -
                       expectedStepProbability) < 1e-15,
          "F2 re-entry retained exposure from the preceding episode");
}

void
CheckComputeFaultCombination()
{
    const double combined = CombineComputeFaultProbabilities(0.2, 0.3);
    Check(std::abs(combined - 0.44) < 1e-15,
          "F1/F2 union probability differs");
    Check(CombineComputeFaultProbabilities(0.0, 0.0) == 0.0 &&
              CombineComputeFaultProbabilities(1.0, 0.3) == 1.0 &&
              CombineComputeFaultProbabilities(0.2, 1.0) == 1.0,
          "F1/F2 union probability boundaries differ");

    const ComputeFaultSourceOutcome both =
        EvaluateComputeFaultSources(0.6, 0.2, 0.7, 0.3);
    Check(both.f1Occurred && both.f2Occurred && both.computeFaultOccurred &&
              std::abs(both.combinedProbability - 0.88) < 1e-15,
          "simultaneous F1/F2 hits were not coalesced");

    const ComputeFaultSourceOutcome f1Only =
        EvaluateComputeFaultSources(0.6, 0.2, 0.7, 0.9);
    const ComputeFaultSourceOutcome f2Only =
        EvaluateComputeFaultSources(0.6, 0.9, 0.7, 0.3);
    const ComputeFaultSourceOutcome neither =
        EvaluateComputeFaultSources(0.6, 0.9, 0.7, 0.9);
    Check(f1Only.f1Occurred && !f1Only.f2Occurred &&
              f1Only.computeFaultOccurred && !f2Only.f1Occurred &&
              f2Only.f2Occurred && f2Only.computeFaultOccurred &&
              !neither.f1Occurred && !neither.f2Occurred &&
              !neither.computeFaultOccurred,
          "independent F1/F2 source outcomes differ");

    bool invalidRejected = false;
    try
    {
        EvaluateComputeFaultSources(-0.1, 0.0, 0.0, 0.0);
    }
    catch (const std::invalid_argument&)
    {
        invalidRejected = true;
    }
    Check(invalidRejected, "invalid compute-fault probability was accepted");
}

void
CheckComputeFailurePrediction()
{
    constexpr int64_t secondNs = 1000000000;
    FaultParameters parameters = GetDefaultFaultParameters();
    const F1SelfStateFaultModel f1Model(parameters.f1);
    F1SelfStateFaultSnapshot f1State = f1Model.CreateInitialSnapshot();
    for (uint32_t second = 0; second < 44; ++second)
    {
        f1Model.Update(f1State, true, 1.0);
    }
    Check(f1State.stepFailureProbability > 0, "F1 forecast fixture has no probability");
    const double currentTemperatureC = f1State.temperatureC;
    const ComputeFailurePrediction f1Prediction =
        PredictComputeFailureBeforeFinish(
            {&f1Model,
             f1State,
             nullptr,
             {},
             {},
             44 * secondNs,
             12 * secondNs,
             secondNs});
    Check(f1Prediction.horizonStepCount == 13 &&
              f1Prediction.steps.size() == 13 &&
              f1Prediction.steps.front().targetTimeNs == 44 * secondNs &&
              f1Prediction.steps.back().targetTimeNs == 56 * secondNs,
          "F1 forecast horizon omitted a scheduled fault check");
    Check(f1Prediction.f1StepFailureProbability ==
                  f1State.stepFailureProbability &&
              f1Prediction.f2StepFailureProbability == 0.0 &&
              f1Prediction.steps.back().f1StepFailureProbability == 1.0 &&
              f1Prediction.predictedFailureProbability == 1.0,
          "F1 forward model did not predict deterministic critical shutdown");
    for (std::size_t index = 1; index < f1Prediction.steps.size(); ++index)
    {
        Check(f1Prediction.steps[index - 1].f1StepFailureProbability <=
                      f1Prediction.steps[index].f1StepFailureProbability &&
                  std::abs(
                      f1Prediction.steps[index].combinedStepFailureProbability -
                      f1Prediction.steps[index].f1StepFailureProbability) < 1e-15,
              "F1 forecast probability trajectory differs");
    }
    Check(f1State.temperatureC == currentTemperatureC,
          "F1 forecast mutated the live input snapshot");

    parameters.f2.referenceSeuIntensityPerSecond = 0.02;
    parameters.f2.seuToComputeFailureProbability = 0.5;
    const F2RadiationFaultModel f2Model(parameters.f2);
    F2RadiationFaultSnapshot f2State = f2Model.CreateInitialSnapshot();
    const Vector insidePosition =
        MakeEcef(parameters.f2.hotspotLatitudeDegrees,
                 parameters.f2.hotspotLongitudeDegrees);
    const Vector outsidePosition = MakeEcef(25.0, 45.0);
    f2Model.Update(f2State, insidePosition, 0.0);
    f2Model.Update(f2State, insidePosition, 1.0);
    const double currentExposureSeconds = f2State.continuousExposureSeconds;
    const ComputeFailurePrediction f2Prediction =
        PredictComputeFailureBeforeFinish(
            {nullptr,
             {},
             &f2Model,
             f2State,
             [insidePosition, outsidePosition](int64_t targetTimeNs) {
                 return targetTimeNs < 12 * secondNs ? insidePosition
                                                     : outsidePosition;
             },
             10 * secondNs,
             3 * secondNs,
             secondNs});
    const double expectedF2StepProbability = -std::expm1(-0.01);
    const double expectedF2WindowProbability =
        1.0 - std::pow(1.0 - expectedF2StepProbability, 2.0);
    Check(f2Prediction.horizonStepCount == 4 &&
              f2Prediction.steps[0].f2StepFailureProbability ==
                  expectedF2StepProbability &&
              f2Prediction.steps[1].f2StepFailureProbability ==
                  expectedF2StepProbability &&
              f2Prediction.steps[2].f2StepFailureProbability == 0.0 &&
              f2Prediction.steps[3].f2StepFailureProbability == 0.0 &&
              std::abs(f2Prediction.predictedFailureProbability -
                       expectedF2WindowProbability) < 1e-15,
          "F2 forward model did not predict exposure exit");
    Check(f2State.continuousExposureSeconds == currentExposureSeconds,
          "F2 forecast mutated the live input snapshot");

    F1SelfStateFaultSnapshot combinedF1;
    combinedF1.stepFailureProbability = 0.2;
    F2RadiationFaultSnapshot combinedF2;
    combinedF2.stepFailureProbability = 0.3;
    const ComputeFailurePrediction combined =
        PredictComputeFailureBeforeFinish(
            {&f1Model,
             combinedF1,
             &f2Model,
             combinedF2,
             [insidePosition](int64_t) { return insidePosition; },
             0,
             0,
             secondNs});
    Check(combined.horizonStepCount == 1 &&
              std::abs(combined.combinedStepFailureProbability - 0.44) < 1e-15 &&
              std::abs(combined.predictedFailureProbability - 0.44) < 1e-15,
          "current F1/F2 union probability differs in the forecast");

    bool negativeRemainingRejected = false;
    try
    {
        ComputeFailurePredictionInput invalid;
        invalid.f1Model = &f1Model;
        invalid.remainingComputeTimeNs = -1;
        invalid.checkIntervalNs = secondNs;
        static_cast<void>(PredictComputeFailureBeforeFinish(invalid));
    }
    catch (const std::invalid_argument&)
    {
        negativeRemainingRejected = true;
    }
    bool zeroIntervalRejected = false;
    try
    {
        ComputeFailurePredictionInput invalid;
        invalid.f1Model = &f1Model;
        invalid.remainingComputeTimeNs = secondNs;
        static_cast<void>(PredictComputeFailureBeforeFinish(invalid));
    }
    catch (const std::invalid_argument&)
    {
        zeroIntervalRejected = true;
    }
    bool missingPositionRejected = false;
    try
    {
        ComputeFailurePredictionInput invalid;
        invalid.f2Model = &f2Model;
        invalid.remainingComputeTimeNs = secondNs;
        invalid.checkIntervalNs = secondNs;
        static_cast<void>(PredictComputeFailureBeforeFinish(invalid));
    }
    catch (const std::invalid_argument&)
    {
        missingPositionRejected = true;
    }
    Check(negativeRemainingRejected && zeroIntervalRejected &&
              missingPositionRejected,
          "invalid compute failure prediction horizon was accepted");
}

void
CheckF3Model()
{
    auto controlled = GetDefaultFaultParameters();
    controlled.f3.enabled = true;
    controlled.f3.mode = "controlled";
    controlled.f3.controlledNodeId = 9;
    controlled.f3.controlledStartSeconds = 1.23456789;
    ValidateFaultParameters(controlled);
    const F3DebrisFaultModel controlledModel(controlled.f3);
    const auto schedule = controlledModel.GenerateSchedule({3, 9}, 2000000000, 11, 12);
    Check(schedule.size() == 1 && schedule[0].nodeId == 9 &&
              schedule[0].startTimeNs == 1234567890,
          "controlled F3 changed its exact target/time");
    bool rejected = false;
    try
    {
        controlledModel.GenerateSchedule({3, 8}, 2000000000, 11, 12);
    }
    catch (const F3DebrisFaultModelError&)
    {
        rejected = true;
    }
    Check(rejected, "controlled F3 accepted an unknown satellite");
    rejected = false;
    try
    {
        controlledModel.GenerateSchedule({3, 9}, 1000000000, 11, 12);
    }
    catch (const F3DebrisFaultModelError&)
    {
        rejected = true;
    }
    Check(rejected, "controlled F3 accepted a time beyond the simulation end");
    FaultParameters parameters = GetDefaultFaultParameters();
    parameters.f3.enabled = true;
    parameters.f3.mode = "fixed_k";
    parameters.f3.fixedCount = 3;
    parameters.f3.singleSatelliteIntensityPerSecond = 0.0;
    const F3DebrisFaultModel fixedModel(parameters.f3);
    const std::vector<uint32_t> satelliteIds = {9, 1, 5, 3};
    const std::vector<F3DebrisFaultEvent> first =
        fixedModel.GenerateSchedule(satelliteIds, 1000000000000LL, 3000000, 3000001);
    const std::vector<F3DebrisFaultEvent> second =
        fixedModel.GenerateSchedule(satelliteIds, 1000000000000LL, 3000000, 3000001);
    Check(first.size() == 3 && second.size() == first.size(),
          "F3 fixed_k did not generate exactly K events");
    std::set<uint32_t> selectedNodes;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        Check(first[index].startTimeNs >= 0 &&
                  first[index].startTimeNs < 1000000000000LL,
              "F3 fixed_k generated a time outside [0, T)");
        Check(index == 0 ||
                  first[index - 1].startTimeNs <= first[index].startTimeNs,
              "F3 fixed_k times are not sorted");
        Check(selectedNodes.insert(first[index].nodeId).second,
              "F3 fixed_k selected one node twice");
        Check(first[index].startTimeNs == second[index].startTimeNs &&
                  first[index].nodeId == second[index].nodeId,
              "F3 fixed_k schedule is not deterministic");
    }

    bool excessiveCountRejected = false;
    try
    {
        fixedModel.GenerateSchedule({1, 2}, 1000, 3000000, 3000001);
    }
    catch (const F3DebrisFaultModelError&)
    {
        excessiveCountRejected = true;
    }
    Check(excessiveCountRejected,
          "F3 fixed_count larger than the constellation was accepted");

    parameters.f3.mode = "poisson";
    parameters.f3.fixedCount = 0;
    parameters.f3.singleSatelliteIntensityPerSecond = 0.01;
    const F3DebrisFaultModel poissonModel(parameters.f3);
    const std::vector<F3DebrisFaultEvent> poissonFirst =
        poissonModel.GenerateSchedule(satelliteIds, 1000000000000LL, 3000000, 3000001);
    const std::vector<F3DebrisFaultEvent> poissonSecond =
        poissonModel.GenerateSchedule(satelliteIds, 1000000000000LL, 3000000, 3000001);
    Check(!poissonFirst.empty() && poissonFirst.size() <= satelliteIds.size() &&
              poissonFirst.size() == poissonSecond.size(),
          "F3 poisson event count is invalid");
    selectedNodes.clear();
    for (std::size_t index = 0; index < poissonFirst.size(); ++index)
    {
        Check(poissonFirst[index].startTimeNs >= 0 &&
                  poissonFirst[index].startTimeNs < 1000000000000LL,
              "F3 poisson generated a time outside [0, T)");
        Check(selectedNodes.insert(poissonFirst[index].nodeId).second,
              "F3 poisson selected one node twice");
        Check(poissonFirst[index].startTimeNs == poissonSecond[index].startTimeNs &&
                  poissonFirst[index].nodeId == poissonSecond[index].nodeId,
              "F3 poisson schedule is not deterministic");
    }
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
        CheckF2Model();
        CheckComputeFaultCombination();
        CheckComputeFailurePrediction();
        CheckF3Model();
        std::cout << "SatCompute fault model tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
