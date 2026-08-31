/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/constellation-definition.h"
#include "ns3/f2-radiation-fault-model.h"
#include "ns3/fault-parameter-validator.h"
#include "ns3/fault-para.h"
#include "ns3/nstime.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

using Json = nlohmann::ordered_json;

constexpr int64_t F2_COMPUTE_FAULT_STREAM_BASE = 2000000;

/** Aggregated observations for one fixed longitude/latitude cell. */
struct SpatialBin
{
    uint64_t exposureSeconds{}; ///< All in-region position samples.
    uint64_t eligibleExposureSeconds{}; ///< Samples at which a draw was allowed.
    double weightedExposureSeconds{}; ///< Sum of spatial risk over all samples.
    double eligibleWeightedExposureSeconds{}; ///< Risk sum over allowed draws.
    double expectedFaultCount{}; ///< Sum of current-step probabilities.
    uint64_t faultCount{}; ///< Actual sampled failures.
};

/** Per-satellite pure-model and recoverable-outage state. */
struct NodeValidationState
{
    F2RadiationFaultSnapshot snapshot; ///< Current pure F2 state.
    Ptr<UniformRandomVariable> random; ///< Stable ns-3 F2 random stream.
    int64_t unavailableUntilSecond{}; ///< Exclusive recoverable-outage endpoint.
};

/** One actual F2 failure retained for the spatial scatter evidence. */
struct SpatialFaultEvent
{
    uint64_t eventId{}; ///< Stable chronological identity.
    int64_t timeSecond{}; ///< Integer simulation second.
    uint32_t nodeId{}; ///< Stable satellite ID.
    double longitudeDegrees{}; ///< Event longitude.
    double latitudeDegrees{}; ///< Event latitude.
    double spatialRisk{}; ///< Current w_F2.
    double stepFailureProbability{}; ///< Current q_F2.
    double randomValue{}; ///< Uniform variate that caused the event.
    bool riskActive{}; ///< Whether w_F2 reached the NOTICE threshold.
    int64_t recoveryTimeSecond{}; ///< Exclusive outage endpoint.
};

/** Compact statistics used to accept the long-horizon spatial run. */
struct SpatialStatistics
{
    double exposureWeightedMeanRisk{}; ///< Mean risk over eligible region samples.
    double eventMeanRisk{}; ///< Mean risk at actual fault locations.
    double highRiskExposureFraction{}; ///< Eligible samples above NOTICE threshold.
    double highRiskFaultFraction{}; ///< Faults above NOTICE threshold.
    double riskRatePearsonCorrelation{}; ///< Bin mean risk versus empirical rate.
    uint64_t correlationBinCount{}; ///< Bins included in the correlation.
    std::size_t peakCountBin{}; ///< Bin with the largest raw event count.
    std::size_t peakRateBin{}; ///< Exposed bin with the largest empirical rate.
};

double
SafeFraction(double numerator, double denominator)
{
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

/** Run native orbit queries and the production F2 probability formula only. */
class F2SpatialValidation
{
  public:
    /**
     * Construct one deterministic long-horizon F2 validation.
     *
     * @param constellation Native orbit source with the requested epoch offset.
     * @param parameters Strictly validated frozen F2 parameters.
     * @param durationSeconds Exclusive simulation duration.
     * @param recoveryDurationSeconds Recoverable compute outage length.
     * @param longitudeBinDegrees Longitude grid width.
     * @param latitudeBinDegrees Latitude grid height.
     * @param progressIntervalSeconds Progress reporting cadence; zero disables it.
     */
    F2SpatialValidation(const OnlineOrbitConstellation& constellation,
                        const F2FaultParameters& parameters,
                        int64_t durationSeconds,
                        int64_t recoveryDurationSeconds,
                        double longitudeBinDegrees,
                        double latitudeBinDegrees,
                        int64_t progressIntervalSeconds)
        : m_constellation(constellation),
          m_model(parameters),
          m_parameters(parameters),
          m_durationSeconds(durationSeconds),
          m_recoveryDurationSeconds(recoveryDurationSeconds),
          m_longitudeBinDegrees(longitudeBinDegrees),
          m_latitudeBinDegrees(latitudeBinDegrees),
          m_progressIntervalSeconds(progressIntervalSeconds),
          m_longitudeBinCount(CalculateBinCount(parameters.longitudeMinDegrees,
                                                parameters.longitudeMaxDegrees,
                                                longitudeBinDegrees)),
          m_latitudeBinCount(CalculateBinCount(parameters.latitudeMinDegrees,
                                               parameters.latitudeMaxDegrees,
                                               latitudeBinDegrees)),
          m_bins(m_longitudeBinCount * m_latitudeBinCount),
          m_nodes(constellation.GetIdMap().GetNodeCount())
    {
        for (uint32_t nodeId = 0; nodeId < m_nodes.size(); ++nodeId)
        {
            NodeValidationState& state = m_nodes[nodeId];
            state.snapshot = m_model.CreateInitialSnapshot();
            state.random = CreateObject<UniformRandomVariable>();
            state.random->SetStream(F2_COMPUTE_FAULT_STREAM_BASE + nodeId);
        }
    }

    /** Observe every satellite at every integer second in [0, duration). */
    void Run()
    {
        for (int64_t second = 0; second < m_durationSeconds; ++second)
        {
            Sample(second);
            if (m_progressIntervalSeconds > 0 && second > 0 &&
                second % m_progressIntervalSeconds == 0)
            {
                std::cerr << "progress_seconds=" << second
                          << " fault_count=" << m_events.size() << std::endl;
            }
        }
    }

    /**
     * Write event, grid, and summary evidence.
     *
     * @param outputDirectory Destination directory.
     * @param constellationConfig Constellation source path for provenance.
     * @param randomSeed ns-3 seed used by this run.
     * @param randomRun ns-3 run number used by this run.
     * @return Ordered summary including acceptance checks.
     */
    Json Finalize(const std::filesystem::path& outputDirectory,
                  const std::filesystem::path& constellationConfig,
                  uint32_t randomSeed,
                  uint64_t randomRun) const
    {
        if (m_events.empty() || m_eligibleRegionExposureSeconds == 0)
        {
            throw std::runtime_error(
                "F2 spatial validation produced no usable fault evidence");
        }
        const SpatialStatistics statistics = CalculateSpatialStatistics();
        const double standardDeviation = std::sqrt(m_expectedFaultVariance);
        const double countZScore =
            standardDeviation > 0.0
                ? (static_cast<double>(m_events.size()) -
                   m_expectedFaultCountWithSuppression) /
                      standardDeviation
                : 0.0;
        const bool sufficientFaultCount = m_events.size() >= 500;
        const bool countWithinFourSigma = std::abs(countZScore) <= 4.0;
        const bool eventRiskConcentrated =
            statistics.eventMeanRisk > statistics.exposureWeightedMeanRisk;
        const bool highRiskConcentrated =
            statistics.highRiskFaultFraction > statistics.highRiskExposureFraction;
        const bool positiveRiskRateCorrelation =
            statistics.correlationBinCount >= 20 &&
            statistics.riskRatePearsonCorrelation > 0.0;
        const bool allPassed = sufficientFaultCount && countWithinFourSigma &&
                               eventRiskConcentrated && highRiskConcentrated &&
                               positiveRiskRateCorrelation;

        const ConstellationDefinition& definition = m_constellation.GetConfig();
        const Json summary = {
            {"validation_scope",
             "orbit-only native positions plus F2 sampling; no network, routing, "
             "tasks, F1, F3, or FaultController"},
            {"constellation",
             {{"config", constellationConfig.filename().string()},
              {"satellite_count", definition.GetSatelliteCount()},
              {"altitude_km", definition.shell.alt},
              {"inclination_deg", definition.shell.inc}}},
            {"run",
             {{"duration_s", m_durationSeconds},
              {"orbit_start_offset_s", m_constellation.GetStartOffsetSeconds()},
              {"sample_interval_s", 1},
              {"recoverable_compute_duration_s", m_recoveryDurationSeconds},
              {"random_seed", randomSeed},
              {"random_run", randomRun},
              {"longitude_bin_deg", m_longitudeBinDegrees},
              {"latitude_bin_deg", m_latitudeBinDegrees}}},
            {"f2_parameters",
             {{"longitude_min_deg", m_parameters.longitudeMinDegrees},
              {"longitude_max_deg", m_parameters.longitudeMaxDegrees},
              {"latitude_min_deg", m_parameters.latitudeMinDegrees},
              {"latitude_max_deg", m_parameters.latitudeMaxDegrees},
              {"hotspot_longitude_deg", m_parameters.hotspotLongitudeDegrees},
              {"hotspot_latitude_deg", m_parameters.hotspotLatitudeDegrees},
              {"sigma_longitude_west_deg",
               m_parameters.sigmaLongitudeWestDegrees},
              {"sigma_longitude_east_deg",
               m_parameters.sigmaLongitudeEastDegrees},
              {"sigma_latitude_deg", m_parameters.sigmaLatitudeDegrees},
              {"spatial_risk_threshold", m_parameters.spatialRiskThreshold},
              {"reference_seu_intensity_per_s",
               m_parameters.referenceSeuIntensityPerSecond},
              {"seu_to_compute_failure_probability",
               m_parameters.seuToComputeFailureProbability},
              {"maximum_effective_failure_intensity_per_s",
               m_model.GetMaximumFailureIntensityPerSecond()}}},
            {"sampling",
             {{"position_sample_count", m_positionSampleCount},
              {"region_exposure_satellite_s", m_regionExposureSeconds},
              {"eligible_region_exposure_satellite_s",
               m_eligibleRegionExposureSeconds},
              {"weighted_region_exposure_satellite_s",
               m_weightedRegionExposureSeconds},
              {"eligible_weighted_region_exposure_satellite_s",
               m_eligibleWeightedRegionExposureSeconds},
              {"random_draw_count", m_randomDrawCount},
              {"recovery_suppressed_draw_count", m_suppressedDrawCount},
              {"expected_fault_count_without_recovery_suppression",
               m_expectedFaultCountWithoutSuppression},
              {"expected_fault_count_with_observed_recovery_suppression",
               m_expectedFaultCountWithSuppression},
              {"actual_fault_count", m_events.size()},
              {"count_z_score", countZScore},
              {"notice_region_fault_count", m_highRiskFaultCount},
              {"unannounced_region_fault_count",
               m_events.size() - m_highRiskFaultCount}}},
            {"spatial_validation",
             {{"exposure_weighted_mean_risk",
               statistics.exposureWeightedMeanRisk},
              {"fault_event_mean_risk", statistics.eventMeanRisk},
              {"high_risk_exposure_fraction",
               statistics.highRiskExposureFraction},
              {"high_risk_fault_fraction", statistics.highRiskFaultFraction},
              {"risk_rate_pearson_correlation",
               statistics.riskRatePearsonCorrelation},
              {"correlation_bin_count", statistics.correlationBinCount},
              {"peak_fault_count_bin", BinJson(statistics.peakCountBin)},
              {"peak_observed_rate_bin", BinJson(statistics.peakRateBin)}}},
            {"acceptance",
             {{"sufficient_fault_count", sufficientFaultCount},
              {"count_within_four_sigma", countWithinFourSigma},
              {"fault_risk_exceeds_exposure_risk", eventRiskConcentrated},
              {"high_risk_fault_fraction_exceeds_exposure_fraction",
               highRiskConcentrated},
              {"positive_bin_risk_rate_correlation",
               positiveRiskRateCorrelation},
              {"all_passed", allPassed}}}};

        std::filesystem::create_directories(outputDirectory);
        WriteEvents(outputDirectory / "n4b-f2-spatial-fault-events.csv");
        WriteBins(outputDirectory / "n4b-f2-spatial-validation-bins.csv");
        std::ofstream output(
            outputDirectory / "n4b-f2-spatial-validation-summary.json",
            std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("cannot write F2 spatial validation summary");
        }
        output << summary.dump(2) << '\n';
        return summary;
    }

  private:
    static std::size_t CalculateBinCount(double minimum,
                                         double maximum,
                                         double width)
    {
        const double count = (maximum - minimum) / width;
        if (!std::isfinite(width) || width <= 0.0 || !std::isfinite(count) ||
            std::abs(count - std::round(count)) > 1e-9)
        {
            throw std::runtime_error(
                "spatial bin width must divide the configured F2 region exactly");
        }
        return static_cast<std::size_t>(std::llround(count));
    }

    std::size_t BinIndex(double longitudeDegrees, double latitudeDegrees) const
    {
        const auto indexFor = [](double coordinate,
                                 double minimum,
                                 double maximum,
                                 double width,
                                 std::size_t count) {
            if (coordinate == maximum)
            {
                return count - 1;
            }
            const double raw = std::floor((coordinate - minimum) / width);
            if (!std::isfinite(raw) || raw < 0.0 ||
                raw >= static_cast<double>(count))
            {
                throw std::runtime_error("in-region F2 coordinate has no spatial bin");
            }
            return static_cast<std::size_t>(raw);
        };
        const std::size_t longitudeIndex =
            indexFor(longitudeDegrees,
                     m_parameters.longitudeMinDegrees,
                     m_parameters.longitudeMaxDegrees,
                     m_longitudeBinDegrees,
                     m_longitudeBinCount);
        const std::size_t latitudeIndex =
            indexFor(latitudeDegrees,
                     m_parameters.latitudeMinDegrees,
                     m_parameters.latitudeMaxDegrees,
                     m_latitudeBinDegrees,
                     m_latitudeBinCount);
        return latitudeIndex * m_longitudeBinCount + longitudeIndex;
    }

    void Sample(int64_t second)
    {
        for (uint32_t nodeId = 0; nodeId < m_nodes.size(); ++nodeId)
        {
            NodeValidationState& state = m_nodes[nodeId];
            m_model.Update(state.snapshot,
                           m_constellation.GetPositionAt(nodeId, Seconds(second)),
                           second == 0 ? 0.0 : 1.0);
            ++m_positionSampleCount;
            SpatialBin* bin = nullptr;
            if (state.snapshot.inRegion)
            {
                bin = &m_bins[BinIndex(state.snapshot.longitudeDegrees,
                                       state.snapshot.latitudeDegrees)];
                ++bin->exposureSeconds;
                bin->weightedExposureSeconds += state.snapshot.spatialRisk;
                ++m_regionExposureSeconds;
                m_weightedRegionExposureSeconds += state.snapshot.spatialRisk;
            }
            if (second == 0)
            {
                continue;
            }

            m_expectedFaultCountWithoutSuppression +=
                state.snapshot.stepFailureProbability;
            if (second < state.unavailableUntilSecond)
            {
                ++m_suppressedDrawCount;
                continue;
            }

            ++m_randomDrawCount;
            const double probability = state.snapshot.stepFailureProbability;
            m_expectedFaultCountWithSuppression += probability;
            m_expectedFaultVariance += probability * (1.0 - probability);
            if (bin != nullptr)
            {
                ++bin->eligibleExposureSeconds;
                bin->eligibleWeightedExposureSeconds += state.snapshot.spatialRisk;
                bin->expectedFaultCount += probability;
                ++m_eligibleRegionExposureSeconds;
                m_eligibleWeightedRegionExposureSeconds += state.snapshot.spatialRisk;
                if (m_model.IsRiskActive(state.snapshot))
                {
                    ++m_highRiskEligibleExposureSeconds;
                }
            }

            const double randomValue = state.random->GetValue();
            if (randomValue >= probability)
            {
                continue;
            }
            if (bin == nullptr)
            {
                throw std::runtime_error("F2 sampled a failure outside its region");
            }
            const bool riskActive = m_model.IsRiskActive(state.snapshot);
            ++bin->faultCount;
            m_eventRiskSum += state.snapshot.spatialRisk;
            m_highRiskFaultCount += riskActive ? 1 : 0;
            state.unavailableUntilSecond = second + m_recoveryDurationSeconds;
            m_events.push_back({m_events.size() + 1,
                                second,
                                nodeId,
                                state.snapshot.longitudeDegrees,
                                state.snapshot.latitudeDegrees,
                                state.snapshot.spatialRisk,
                                probability,
                                randomValue,
                                riskActive,
                                state.unavailableUntilSecond});
        }
    }

    SpatialStatistics CalculateSpatialStatistics() const
    {
        SpatialStatistics statistics;
        statistics.exposureWeightedMeanRisk =
            SafeFraction(m_eligibleWeightedRegionExposureSeconds,
                         static_cast<double>(m_eligibleRegionExposureSeconds));
        statistics.eventMeanRisk =
            SafeFraction(m_eventRiskSum, static_cast<double>(m_events.size()));
        statistics.highRiskExposureFraction =
            SafeFraction(static_cast<double>(m_highRiskEligibleExposureSeconds),
                         static_cast<double>(m_eligibleRegionExposureSeconds));
        statistics.highRiskFaultFraction =
            SafeFraction(static_cast<double>(m_highRiskFaultCount),
                         static_cast<double>(m_events.size()));

        std::vector<double> meanRisks;
        std::vector<double> observedRates;
        uint64_t peakCount = 0;
        double peakRate = -1.0;
        for (std::size_t index = 0; index < m_bins.size(); ++index)
        {
            const SpatialBin& bin = m_bins[index];
            if (bin.faultCount > peakCount)
            {
                peakCount = bin.faultCount;
                statistics.peakCountBin = index;
            }
            if (bin.eligibleExposureSeconds == 0)
            {
                continue;
            }
            const double rate = SafeFraction(
                static_cast<double>(bin.faultCount),
                static_cast<double>(bin.eligibleExposureSeconds));
            if (bin.eligibleExposureSeconds >= 1000 && rate > peakRate)
            {
                peakRate = rate;
                statistics.peakRateBin = index;
            }
            if (bin.eligibleExposureSeconds >= 1000 &&
                bin.expectedFaultCount >= 0.25)
            {
                meanRisks.push_back(
                    bin.eligibleWeightedExposureSeconds /
                    static_cast<double>(bin.eligibleExposureSeconds));
                observedRates.push_back(rate);
            }
        }
        statistics.correlationBinCount = meanRisks.size();
        if (meanRisks.size() < 2)
        {
            return statistics;
        }
        const double meanRisk =
            std::accumulate(meanRisks.begin(), meanRisks.end(), 0.0) /
            static_cast<double>(meanRisks.size());
        const double meanRate =
            std::accumulate(observedRates.begin(), observedRates.end(), 0.0) /
            static_cast<double>(observedRates.size());
        double covariance = 0.0;
        double riskVariance = 0.0;
        double rateVariance = 0.0;
        for (std::size_t index = 0; index < meanRisks.size(); ++index)
        {
            const double riskDelta = meanRisks[index] - meanRisk;
            const double rateDelta = observedRates[index] - meanRate;
            covariance += riskDelta * rateDelta;
            riskVariance += riskDelta * riskDelta;
            rateVariance += rateDelta * rateDelta;
        }
        if (riskVariance > 0.0 && rateVariance > 0.0)
        {
            statistics.riskRatePearsonCorrelation =
                covariance / std::sqrt(riskVariance * rateVariance);
        }
        return statistics;
    }

    Json BinJson(std::size_t index) const
    {
        const std::size_t latitudeIndex = index / m_longitudeBinCount;
        const std::size_t longitudeIndex = index % m_longitudeBinCount;
        const double longitudeMinimum =
            m_parameters.longitudeMinDegrees +
            static_cast<double>(longitudeIndex) * m_longitudeBinDegrees;
        const double latitudeMinimum =
            m_parameters.latitudeMinDegrees +
            static_cast<double>(latitudeIndex) * m_latitudeBinDegrees;
        const SpatialBin& bin = m_bins.at(index);
        return {{"longitude_center_deg",
                 longitudeMinimum + m_longitudeBinDegrees / 2.0},
                {"latitude_center_deg",
                 latitudeMinimum + m_latitudeBinDegrees / 2.0},
                {"eligible_exposure_s", bin.eligibleExposureSeconds},
                {"fault_count", bin.faultCount},
                {"fault_rate_per_million_exposure_s",
                 1e6 * SafeFraction(
                           static_cast<double>(bin.faultCount),
                           static_cast<double>(bin.eligibleExposureSeconds))}};
    }

    void WriteEvents(const std::filesystem::path& path) const
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("cannot write " + path.string());
        }
        output << "event_id,time_s,time_ns,node_id,longitude_deg,latitude_deg,"
                  "spatial_risk,step_failure_probability,random_value,risk_active,"
                  "recovery_time_s\n";
        output << std::setprecision(17) << std::boolalpha;
        for (const SpatialFaultEvent& event : m_events)
        {
            output << event.eventId << ',' << event.timeSecond << ','
                   << Seconds(event.timeSecond).GetNanoSeconds() << ','
                   << event.nodeId << ',' << event.longitudeDegrees << ','
                   << event.latitudeDegrees << ',' << event.spatialRisk << ','
                   << event.stepFailureProbability << ',' << event.randomValue << ','
                   << event.riskActive << ',' << event.recoveryTimeSecond << '\n';
        }
    }

    void WriteBins(const std::filesystem::path& path) const
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("cannot write " + path.string());
        }
        output << "longitude_min_deg,longitude_max_deg,longitude_center_deg,"
                  "latitude_min_deg,latitude_max_deg,latitude_center_deg,"
                  "exposure_s,eligible_exposure_s,weighted_exposure_s,"
                  "eligible_weighted_exposure_s,mean_eligible_spatial_risk,"
                  "expected_fault_count,fault_count,"
                  "fault_rate_per_million_exposure_s\n";
        output << std::setprecision(17);
        for (std::size_t latitudeIndex = 0;
             latitudeIndex < m_latitudeBinCount;
             ++latitudeIndex)
        {
            for (std::size_t longitudeIndex = 0;
                 longitudeIndex < m_longitudeBinCount;
                 ++longitudeIndex)
            {
                const std::size_t index =
                    latitudeIndex * m_longitudeBinCount + longitudeIndex;
                const SpatialBin& bin = m_bins[index];
                const double longitudeMinimum =
                    m_parameters.longitudeMinDegrees +
                    static_cast<double>(longitudeIndex) * m_longitudeBinDegrees;
                const double latitudeMinimum =
                    m_parameters.latitudeMinDegrees +
                    static_cast<double>(latitudeIndex) * m_latitudeBinDegrees;
                output << longitudeMinimum << ','
                       << longitudeMinimum + m_longitudeBinDegrees << ','
                       << longitudeMinimum + m_longitudeBinDegrees / 2.0 << ','
                       << latitudeMinimum << ','
                       << latitudeMinimum + m_latitudeBinDegrees << ','
                       << latitudeMinimum + m_latitudeBinDegrees / 2.0 << ','
                       << bin.exposureSeconds << ',' << bin.eligibleExposureSeconds
                       << ',' << bin.weightedExposureSeconds << ','
                       << bin.eligibleWeightedExposureSeconds << ','
                       << SafeFraction(
                              bin.eligibleWeightedExposureSeconds,
                              static_cast<double>(bin.eligibleExposureSeconds))
                       << ',' << bin.expectedFaultCount << ',' << bin.faultCount << ','
                       << 1e6 * SafeFraction(
                                      static_cast<double>(bin.faultCount),
                                      static_cast<double>(bin.eligibleExposureSeconds))
                       << '\n';
            }
        }
    }

    const OnlineOrbitConstellation& m_constellation; ///< Native orbit source.
    F2RadiationFaultModel m_model; ///< Production pure F2 model.
    F2FaultParameters m_parameters; ///< Frozen parameter provenance.
    int64_t m_durationSeconds{}; ///< Exclusive run duration.
    int64_t m_recoveryDurationSeconds{}; ///< Recoverable outage duration.
    double m_longitudeBinDegrees{}; ///< Longitude grid width.
    double m_latitudeBinDegrees{}; ///< Latitude grid height.
    int64_t m_progressIntervalSeconds{}; ///< Progress report cadence.
    std::size_t m_longitudeBinCount{}; ///< Number of longitude cells.
    std::size_t m_latitudeBinCount{}; ///< Number of latitude cells.
    std::vector<SpatialBin> m_bins; ///< Row-major latitude/longitude cells.
    std::vector<NodeValidationState> m_nodes; ///< Per-satellite sampling states.
    std::vector<SpatialFaultEvent> m_events; ///< Chronological actual events.
    uint64_t m_positionSampleCount{}; ///< All native position observations.
    uint64_t m_regionExposureSeconds{}; ///< In-region position samples.
    uint64_t m_eligibleRegionExposureSeconds{}; ///< In-region allowed draws.
    uint64_t m_highRiskEligibleExposureSeconds{}; ///< High-risk allowed draws.
    uint64_t m_randomDrawCount{}; ///< All available-node F2 random draws.
    uint64_t m_suppressedDrawCount{}; ///< Draws skipped during recovery.
    uint64_t m_highRiskFaultCount{}; ///< Events at or above NOTICE threshold.
    double m_weightedRegionExposureSeconds{}; ///< Risk sum over region samples.
    double m_eligibleWeightedRegionExposureSeconds{}; ///< Risk sum over draws.
    double m_expectedFaultCountWithoutSuppression{}; ///< Sum q over all checks.
    double m_expectedFaultCountWithSuppression{}; ///< Sum q over allowed checks.
    double m_expectedFaultVariance{}; ///< Bernoulli variance over allowed checks.
    double m_eventRiskSum{}; ///< Risk sum at actual event locations.
};

} // namespace

int
main(int argc, char* argv[])
{
    FaultParameters parameters = GetDefaultFaultParameters();
    std::string constellationConfig;
    std::string outputDirectory;
    int64_t durationSeconds = 500000;
    double orbitStartOffsetSeconds = 302.0;
    double longitudeBinDegrees = 2.5;
    double latitudeBinDegrees = 2.5;
    int64_t progressIntervalSeconds = 50000;
    uint32_t randomSeed = 1;
    uint64_t randomRun = 1;
    CommandLine command(__FILE__);
    command.AddValue("constellationConfig",
                     "Native ns-3.48 constellation CSV",
                     constellationConfig);
    command.AddValue("duration",
                     "Exclusive orbit-only validation duration in seconds",
                     durationSeconds);
    command.AddValue("orbitStartOffset",
                     "Orbit epoch offset at validation time zero in seconds",
                     orbitStartOffsetSeconds);
    command.AddValue("longitudeBin",
                     "Longitude density-bin width in degrees",
                     longitudeBinDegrees);
    command.AddValue("latitudeBin",
                     "Latitude density-bin width in degrees",
                     latitudeBinDegrees);
    command.AddValue("progressInterval",
                     "Progress reporting interval in seconds; zero disables",
                     progressIntervalSeconds);
    command.AddValue("randomSeed", "ns-3 random seed", randomSeed);
    command.AddValue("randomRun", "ns-3 random run number", randomRun);
    command.AddValue("outputDir", "Validation output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        if (constellationConfig.empty() || outputDirectory.empty())
        {
            throw std::runtime_error("constellationConfig and outputDir are required");
        }
        if (durationSeconds <= 1 || !std::isfinite(orbitStartOffsetSeconds) ||
            orbitStartOffsetSeconds < 0.0 || progressIntervalSeconds < 0 ||
            randomSeed == 0 || randomRun == 0)
        {
            throw std::runtime_error("F2 spatial validation run parameters are invalid");
        }
        ValidateFaultParameters(parameters);
        if (parameters.checkIntervalSeconds != 1.0)
        {
            throw std::runtime_error(
                "F2 spatial validation requires a one-second check interval");
        }
        const double recoveryDuration =
            parameters.recoverableComputeDurationSeconds;
        if (!std::isfinite(recoveryDuration) || recoveryDuration <= 0.0 ||
            std::floor(recoveryDuration) != recoveryDuration)
        {
            throw std::runtime_error(
                "F2 spatial validation requires an integer-second recovery duration");
        }

        RngSeedManager::SetSeed(randomSeed);
        RngSeedManager::SetRun(randomRun);
        const ConstellationDefinition definition =
            LoadConstellationDefinition(constellationConfig);
        OnlineOrbitConstellation constellation(definition, orbitStartOffsetSeconds);
        F2SpatialValidation validation(
            constellation,
            parameters.f2,
            durationSeconds,
            static_cast<int64_t>(recoveryDuration),
            longitudeBinDegrees,
            latitudeBinDegrees,
            progressIntervalSeconds);
        validation.Run();
        const Json summary = validation.Finalize(outputDirectory,
                                                 constellationConfig,
                                                 randomSeed,
                                                 randomRun);
        Simulator::Destroy();
        std::cout << (summary["acceptance"]["all_passed"].get<bool>() ? "PASS" : "FAIL")
                  << " faults=" << summary["sampling"]["actual_fault_count"]
                  << " expected="
                  << summary["sampling"]
                            ["expected_fault_count_with_observed_recovery_suppression"]
                  << " correlation="
                  << summary["spatial_validation"]["risk_rate_pearson_correlation"]
                  << std::endl;
        return summary["acceptance"]["all_passed"].get<bool>() ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
