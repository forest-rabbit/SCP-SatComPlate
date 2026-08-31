/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/constellation-definition.h"
#include "ns3/f2-radiation-fault-model.h"
#include "ns3/fault-parameter-validator.h"
#include "ns3/fault-para.h"
#include "ns3/online-orbit-constellation.h"
#include "ns3/simulator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace ns3;

namespace
{

using Json = nlohmann::ordered_json;

/** One observed continuous F2-region exposure episode. */
struct ExposureEpisode
{
    uint64_t episodeId{}; ///< Stable ID assigned after chronological sorting.
    uint32_t nodeId{}; ///< Stable satellite ID.
    int64_t entryTimeNs{}; ///< First in-window inside sample.
    std::optional<int64_t> exitTimeNs; ///< First outside sample, when observed.
    int64_t exposureDurationNs{}; ///< Duration observed within the calibration window.
    bool leftCensored{}; ///< Whether the satellite was already inside at time zero.
    bool rightCensored{}; ///< Whether the satellite remained inside at the end.
};

/** Open-episode and pure-model state for one satellite. */
struct NodeExposureState
{
    F2RadiationFaultSnapshot snapshot; ///< Current pure F2 state.
    std::optional<int64_t> entryTimeNs; ///< Open episode entry boundary.
    bool leftCensored{}; ///< Open episode started before the calibration window.
    std::optional<int64_t> highRiskEntryTimeNs; ///< Open NOTICE-region entry.
    bool highRiskLeftCensored{}; ///< High-risk episode started before the window.
};

/** One fixed-length constellation exposure window. */
struct ExposureWindow
{
    int64_t startOffsetSeconds{}; ///< Window start relative to orbit time zero.
    int64_t totalExposureSeconds{}; ///< Sum of satellite-seconds inside the region.
    double weightedExposureSeconds{}; ///< Spatial-risk-weighted satellite-seconds.
    int64_t highRiskExposureSeconds{}; ///< Satellite-seconds at or above notice risk.
    uint64_t overlappingEpisodeCount{}; ///< Episodes with any exposure in the window.
    uint64_t completeEpisodeCount{}; ///< Episodes entering and exiting within the window.
    uint64_t leftCensoredEpisodeCount{}; ///< Episodes already active at window start.
    uint64_t rightCensoredEpisodeCount{}; ///< Episodes active at window end.
};

bool
HasPreferredEpisodeCoverage(const ExposureWindow& window)
{
    return window.completeEpisodeCount > 0 &&
           (window.leftCensoredEpisodeCount > 0 ||
            window.rightCensoredEpisodeCount > 0);
}

double
Median(std::vector<double> values)
{
    if (values.empty())
    {
        throw std::runtime_error("cannot calculate a median from no values");
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1)
    {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

Json
DistributionJson(const std::vector<double>& values)
{
    if (values.empty())
    {
        return {{"count", 0},
                {"min", nullptr},
                {"median", nullptr},
                {"mean", nullptr},
                {"max", nullptr}};
    }
    const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
    return {{"count", values.size()},
            {"min", *minimum},
            {"median", Median(values)},
            {"mean", std::accumulate(values.begin(), values.end(), 0.0) /
                         static_cast<double>(values.size())},
            {"max", *maximum}};
}

/** Run one orbit-only exposure scan and retain compact calibration evidence. */
class ExposureCalibration
{
  public:
    /**
     * Construct a scan over one native constellation.
     *
     * @param constellation Live native ns-3 orbit constellation.
     * @param parameters Strictly validated F2 parameters.
     * @param calibrationDurationSeconds Total scan duration.
     * @param windowDurationSeconds Sliding functional-window duration.
     */
    ExposureCalibration(const OnlineOrbitConstellation& constellation,
                        const F2FaultParameters& parameters,
                        int64_t calibrationDurationSeconds,
                        int64_t windowDurationSeconds)
        : m_constellation(constellation),
          m_model(parameters),
          m_parameters(parameters),
          m_calibrationDurationSeconds(calibrationDurationSeconds),
          m_windowDurationSeconds(windowDurationSeconds),
          m_nodes(constellation.GetIdMap().GetNodeCount()),
          m_insideCounts(static_cast<std::size_t>(calibrationDurationSeconds), 0),
          m_weightedRiskSums(static_cast<std::size_t>(calibrationDurationSeconds), 0.0),
          m_highRiskCounts(static_cast<std::size_t>(calibrationDurationSeconds), 0)
    {
        for (NodeExposureState& state : m_nodes)
        {
            state.snapshot = m_model.CreateInitialSnapshot();
        }
    }

    /** Run all one-second observations through the native arbitrary-time query. */
    void Run()
    {
        for (int64_t second = 0; second <= m_calibrationDurationSeconds; ++second)
        {
            Sample(second);
        }
    }

    /**
     * Close censored episodes and write canonical CSV and JSON outputs.
     *
     * @param outputDirectory Existing or creatable calibration output directory.
     * @param constellationConfig User-provided constellation path for provenance.
     * @param functionalTargetMeanFaultCount Target mean faults in the selected window.
     * @param referenceMaximumFailureIntensity Optional fixed hotspot effective
     *        intensity used to validate scaling on a different constellation.
     * @return Complete ordered JSON summary.
     */
    Json Finalize(const std::filesystem::path& outputDirectory,
                  const std::filesystem::path& constellationConfig,
                  double functionalTargetMeanFaultCount,
                  const std::optional<double>& referenceMaximumFailureIntensity)
    {
        const int64_t endTimeNs = Seconds(m_calibrationDurationSeconds).GetNanoSeconds();
        for (std::size_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex)
        {
            const uint32_t nodeId = static_cast<uint32_t>(nodeIndex);
            NodeExposureState& state = m_nodes[nodeId];
            if (state.entryTimeNs.has_value())
            {
                m_episodes.push_back({0,
                                      nodeId,
                                      state.entryTimeNs.value(),
                                      std::nullopt,
                                      endTimeNs - state.entryTimeNs.value(),
                                      state.leftCensored,
                                      true});
                state.entryTimeNs.reset();
            }
            if (state.highRiskEntryTimeNs.has_value())
            {
                m_highRiskEpisodes.push_back(
                    {0,
                     nodeId,
                     state.highRiskEntryTimeNs.value(),
                     std::nullopt,
                     endTimeNs - state.highRiskEntryTimeNs.value(),
                     state.highRiskLeftCensored,
                     true});
                state.highRiskEntryTimeNs.reset();
            }
        }
        std::sort(m_episodes.begin(),
                  m_episodes.end(),
                  [](const ExposureEpisode& left, const ExposureEpisode& right) {
                      return std::tie(left.entryTimeNs, left.nodeId) <
                             std::tie(right.entryTimeNs, right.nodeId);
                  });
        for (std::size_t index = 0; index < m_episodes.size(); ++index)
        {
            m_episodes[index].episodeId = index + 1;
        }
        std::sort(m_highRiskEpisodes.begin(),
                  m_highRiskEpisodes.end(),
                  [](const ExposureEpisode& left, const ExposureEpisode& right) {
                      return std::tie(left.entryTimeNs, left.nodeId) <
                             std::tie(right.entryTimeNs, right.nodeId);
                  });
        for (std::size_t index = 0; index < m_highRiskEpisodes.size(); ++index)
        {
            m_highRiskEpisodes[index].episodeId = index + 1;
        }

        const std::vector<ExposureWindow> windows = BuildWindows();
        std::vector<ExposureWindow> candidates = windows;
        std::sort(candidates.begin(),
                  candidates.end(),
                  [](const ExposureWindow& left, const ExposureWindow& right) {
                      const bool leftPreferred = HasPreferredEpisodeCoverage(left);
                      const bool rightPreferred = HasPreferredEpisodeCoverage(right);
                      if (leftPreferred != rightPreferred)
                      {
                          return leftPreferred;
                      }
                      if (left.weightedExposureSeconds != right.weightedExposureSeconds)
                      {
                          return left.weightedExposureSeconds >
                                 right.weightedExposureSeconds;
                      }
                      if (left.totalExposureSeconds != right.totalExposureSeconds)
                      {
                          return left.totalExposureSeconds > right.totalExposureSeconds;
                      }
                      return left.startOffsetSeconds < right.startOffsetSeconds;
                  });
        if (candidates.empty() || candidates.front().weightedExposureSeconds <= 0.0)
        {
            throw std::runtime_error("calibration found no non-zero F2 exposure window");
        }

        std::vector<double> completeDurationsSeconds;
        std::vector<double> completeHighRiskDurationsSeconds;
        uint64_t leftCensoredCount = 0;
        uint64_t rightCensoredCount = 0;
        uint64_t highRiskLeftCensoredCount = 0;
        uint64_t highRiskRightCensoredCount = 0;
        for (const ExposureEpisode& episode : m_episodes)
        {
            leftCensoredCount += episode.leftCensored ? 1 : 0;
            rightCensoredCount += episode.rightCensored ? 1 : 0;
            if (!episode.leftCensored && !episode.rightCensored)
            {
                completeDurationsSeconds.push_back(
                    static_cast<double>(episode.exposureDurationNs) / 1e9);
            }
        }
        for (const ExposureEpisode& episode : m_highRiskEpisodes)
        {
            highRiskLeftCensoredCount += episode.leftCensored ? 1 : 0;
            highRiskRightCensoredCount += episode.rightCensored ? 1 : 0;
            if (!episode.leftCensored && !episode.rightCensored)
            {
                completeHighRiskDurationsSeconds.push_back(
                    static_cast<double>(episode.exposureDurationNs) / 1e9);
            }
        }
        if (completeDurationsSeconds.size() < 5)
        {
            throw std::runtime_error(
                "calibration requires at least five complete F2 exposure episodes");
        }

        std::vector<double> windowExposureSeconds;
        std::vector<double> windowWeightedExposureSeconds;
        std::vector<double> windowHighRiskExposureSeconds;
        windowExposureSeconds.reserve(windows.size());
        windowWeightedExposureSeconds.reserve(windows.size());
        windowHighRiskExposureSeconds.reserve(windows.size());
        for (const ExposureWindow& window : windows)
        {
            windowExposureSeconds.push_back(window.totalExposureSeconds);
            windowWeightedExposureSeconds.push_back(window.weightedExposureSeconds);
            windowHighRiskExposureSeconds.push_back(window.highRiskExposureSeconds);
        }
        const ExposureWindow selectedWindow = candidates.front();
        const double candidateMaximumFailureIntensity =
            functionalTargetMeanFaultCount /
            selectedWindow.weightedExposureSeconds;
        if (m_parameters.seuToComputeFailureProbability <= 0.0)
        {
            throw std::runtime_error(
                "F2 spatial calibration requires a positive SEU mapping probability");
        }
        const double candidateReferenceSeuIntensity =
            candidateMaximumFailureIntensity /
            m_parameters.seuToComputeFailureProbability;

        Json candidateJson = Json::array();
        const std::size_t candidateCount = std::min<std::size_t>(10, candidates.size());
        for (std::size_t index = 0; index < candidateCount; ++index)
        {
            candidateJson.push_back(
                {{"start_offset_s", candidates[index].startOffsetSeconds},
                 {"total_exposure_s", candidates[index].totalExposureSeconds},
                 {"weighted_exposure_s", candidates[index].weightedExposureSeconds},
                 {"high_risk_exposure_s", candidates[index].highRiskExposureSeconds},
                 {"overlapping_episode_count",
                  candidates[index].overlappingEpisodeCount},
                 {"complete_episode_count", candidates[index].completeEpisodeCount},
                 {"left_censored_episode_count",
                  candidates[index].leftCensoredEpisodeCount},
                 {"right_censored_episode_count",
                  candidates[index].rightCensoredEpisodeCount}});
        }
        const int64_t totalExposureSeconds =
            std::accumulate(m_insideCounts.begin(), m_insideCounts.end(), int64_t{0});
        const double totalWeightedExposureSeconds =
            std::accumulate(m_weightedRiskSums.begin(),
                            m_weightedRiskSums.end(),
                            0.0);
        const int64_t totalHighRiskExposureSeconds =
            std::accumulate(m_highRiskCounts.begin(),
                            m_highRiskCounts.end(),
                            int64_t{0});
        const ConstellationDefinition& definition = m_constellation.GetConfig();
        Json referenceValidation = nullptr;
        if (referenceMaximumFailureIntensity.has_value())
        {
            referenceValidation = {
                {"maximum_effective_failure_intensity_per_s",
                 referenceMaximumFailureIntensity.value()},
                {"expected_fault_count",
                 referenceMaximumFailureIntensity.value() *
                     selectedWindow.weightedExposureSeconds}};
        }
        const Json summary = {
            {"calibration_scope",
             "orbit-only F2 spatial risk; no network, tasks, F1, F3, or fault sampling"},
            {"constellation_config", constellationConfig.filename().string()},
            {"satellite_count", definition.GetSatelliteCount()},
            {"altitude_km", definition.shell.alt},
            {"inclination_deg", definition.shell.inc},
            {"calibration_duration_s", m_calibrationDurationSeconds},
            {"sample_interval_s", 1},
            {"region",
             {{"longitude_min_deg", m_parameters.longitudeMinDegrees},
              {"longitude_max_deg", m_parameters.longitudeMaxDegrees},
              {"latitude_min_deg", m_parameters.latitudeMinDegrees},
              {"latitude_max_deg", m_parameters.latitudeMaxDegrees},
              {"hotspot_longitude_deg", m_parameters.hotspotLongitudeDegrees},
              {"hotspot_latitude_deg", m_parameters.hotspotLatitudeDegrees},
              {"sigma_longitude_deg", m_parameters.sigmaLongitudeDegrees},
              {"sigma_latitude_deg", m_parameters.sigmaLatitudeDegrees},
              {"spatial_risk_threshold", m_parameters.spatialRiskThreshold}}},
            {"seu_mapping",
             {{"seu_to_compute_failure_probability",
               m_parameters.seuToComputeFailureProbability},
              {"configured_reference_seu_intensity_per_s",
               m_parameters.referenceSeuIntensityPerSecond},
              {"configured_maximum_effective_failure_intensity_per_s",
               m_model.GetMaximumFailureIntensityPerSecond()}}},
            {"episode_statistics",
             {{"entering_satellite_count", m_enteringNodeIds.size()},
              {"complete_episode_count", completeDurationsSeconds.size()},
              {"left_censored_episode_count", leftCensoredCount},
              {"right_censored_episode_count", rightCensoredCount},
              {"complete_exposure_duration_s",
               DistributionJson(completeDurationsSeconds)},
              {"total_constellation_exposure_s", totalExposureSeconds},
              {"total_weighted_exposure_s", totalWeightedExposureSeconds},
              {"total_high_risk_exposure_s", totalHighRiskExposureSeconds}}},
            {"high_risk_episode_statistics",
             {{"episode_count", m_highRiskEpisodes.size()},
              {"complete_episode_count",
               completeHighRiskDurationsSeconds.size()},
              {"left_censored_episode_count", highRiskLeftCensoredCount},
              {"right_censored_episode_count", highRiskRightCensoredCount},
              {"complete_duration_s",
               DistributionJson(completeHighRiskDurationsSeconds)}}},
            {"sliding_window",
             {{"duration_s", m_windowDurationSeconds},
              {"exposure_s", DistributionJson(windowExposureSeconds)},
              {"weighted_exposure_s",
               DistributionJson(windowWeightedExposureSeconds)},
              {"high_risk_exposure_s",
               DistributionJson(windowHighRiskExposureSeconds)},
              {"candidate_start_offsets", candidateJson}}},
            {"selected",
             {{"start_offset_s", selectedWindow.startOffsetSeconds},
              {"window_total_exposure_s", selectedWindow.totalExposureSeconds},
              {"window_weighted_exposure_s",
               selectedWindow.weightedExposureSeconds},
              {"window_high_risk_exposure_s",
               selectedWindow.highRiskExposureSeconds},
              {"overlapping_episode_count", selectedWindow.overlappingEpisodeCount},
              {"complete_episode_count", selectedWindow.completeEpisodeCount},
              {"left_censored_episode_count",
               selectedWindow.leftCensoredEpisodeCount},
              {"right_censored_episode_count",
               selectedWindow.rightCensoredEpisodeCount},
              {"functional_target_mean_fault_count",
               functionalTargetMeanFaultCount},
              {"candidate_maximum_effective_failure_intensity_per_s",
               candidateMaximumFailureIntensity},
              {"candidate_reference_seu_intensity_per_s",
               candidateReferenceSeuIntensity}}},
            {"fixed_reference_validation", referenceValidation}};

        std::filesystem::create_directories(outputDirectory);
        WriteEpisodes(outputDirectory / "n4b-f2-spatial-exposure-episodes.csv");
        std::ofstream summaryOutput(
            outputDirectory / "n4b-f2-spatial-calibration-summary.json",
            std::ios::out | std::ios::trunc);
        if (!summaryOutput.is_open())
        {
            throw std::runtime_error("cannot write F2 exposure calibration summary");
        }
        summaryOutput << summary.dump(2) << '\n';
        return summary;
    }

  private:
    /** Observe all native mobility positions at one integer second. */
    void Sample(int64_t second)
    {
        const int64_t nowNs = Seconds(second).GetNanoSeconds();
        uint32_t insideCount = 0;
        uint32_t highRiskCount = 0;
        double weightedRiskSum = 0.0;
        for (std::size_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex)
        {
            const uint32_t nodeId = static_cast<uint32_t>(nodeIndex);
            NodeExposureState& state = m_nodes[nodeId];
            const bool wasInRegion = state.snapshot.inRegion;
            const bool wasHighRisk = m_model.IsRiskActive(state.snapshot);
            m_model.Update(state.snapshot,
                           m_constellation.GetPositionAt(nodeId, Seconds(second)),
                           second == 0 ? 0.0 : 1.0);
            const bool isHighRisk = m_model.IsRiskActive(state.snapshot);
            if (!wasInRegion && state.snapshot.inRegion)
            {
                state.entryTimeNs = nowNs;
                state.leftCensored = second == 0;
                if (second != 0)
                {
                    m_enteringNodeIds.insert(nodeId);
                }
            }
            else if (wasInRegion && !state.snapshot.inRegion)
            {
                if (!state.entryTimeNs.has_value())
                {
                    throw std::runtime_error("F2 exit has no open exposure episode");
                }
                m_episodes.push_back({0,
                                      nodeId,
                                      state.entryTimeNs.value(),
                                      nowNs,
                                      nowNs - state.entryTimeNs.value(),
                                      state.leftCensored,
                                      false});
                state.entryTimeNs.reset();
                state.leftCensored = false;
            }
            if (!wasHighRisk && isHighRisk)
            {
                state.highRiskEntryTimeNs = nowNs;
                state.highRiskLeftCensored = second == 0;
            }
            else if (wasHighRisk && !isHighRisk)
            {
                if (!state.highRiskEntryTimeNs.has_value())
                {
                    throw std::runtime_error(
                        "F2 high-risk exit has no open episode");
                }
                m_highRiskEpisodes.push_back(
                    {0,
                     nodeId,
                     state.highRiskEntryTimeNs.value(),
                     nowNs,
                     nowNs - state.highRiskEntryTimeNs.value(),
                     state.highRiskLeftCensored,
                     false});
                state.highRiskEntryTimeNs.reset();
                state.highRiskLeftCensored = false;
            }
            if (state.snapshot.inRegion)
            {
                ++insideCount;
                weightedRiskSum += state.snapshot.spatialRisk;
                highRiskCount += isHighRisk ? 1 : 0;
            }
        }
        if (second < m_calibrationDurationSeconds)
        {
            m_insideCounts[static_cast<std::size_t>(second)] = insideCount;
            m_weightedRiskSums[static_cast<std::size_t>(second)] = weightedRiskSum;
            m_highRiskCounts[static_cast<std::size_t>(second)] = highRiskCount;
        }
    }

    /** @return Every fixed-length window in ascending start-time order. */
    std::vector<ExposureWindow> BuildWindows() const
    {
        std::vector<int64_t> prefix(m_insideCounts.size() + 1, 0);
        std::vector<double> weightedPrefix(m_weightedRiskSums.size() + 1, 0.0);
        std::vector<int64_t> highRiskPrefix(m_highRiskCounts.size() + 1, 0);
        for (std::size_t index = 0; index < m_insideCounts.size(); ++index)
        {
            prefix[index + 1] = prefix[index] + m_insideCounts[index];
            weightedPrefix[index + 1] =
                weightedPrefix[index] + m_weightedRiskSums[index];
            highRiskPrefix[index + 1] =
                highRiskPrefix[index] + m_highRiskCounts[index];
        }
        std::vector<ExposureWindow> windows;
        for (int64_t start = 0;
             start + m_windowDurationSeconds <= m_calibrationDurationSeconds;
             ++start)
        {
            const int64_t end = start + m_windowDurationSeconds;
            ExposureWindow window;
            window.startOffsetSeconds = start;
            window.totalExposureSeconds =
                prefix[static_cast<std::size_t>(end)] -
                prefix[static_cast<std::size_t>(start)];
            window.weightedExposureSeconds =
                weightedPrefix[static_cast<std::size_t>(end)] -
                weightedPrefix[static_cast<std::size_t>(start)];
            window.highRiskExposureSeconds =
                highRiskPrefix[static_cast<std::size_t>(end)] -
                highRiskPrefix[static_cast<std::size_t>(start)];
            const int64_t startNs = Seconds(start).GetNanoSeconds();
            const int64_t endNs = Seconds(end).GetNanoSeconds();
            for (const ExposureEpisode& episode : m_episodes)
            {
                const int64_t episodeEndNs = episode.exitTimeNs.value_or(
                    Seconds(m_calibrationDurationSeconds).GetNanoSeconds());
                if (episode.entryTimeNs >= endNs || episodeEndNs <= startNs)
                {
                    continue;
                }
                ++window.overlappingEpisodeCount;
                const bool leftCensored = episode.entryTimeNs < startNs ||
                                          (episode.entryTimeNs == startNs &&
                                           episode.leftCensored);
                const bool rightCensored = episodeEndNs > endNs ||
                                           (episodeEndNs == endNs &&
                                            episode.rightCensored);
                window.leftCensoredEpisodeCount += leftCensored ? 1 : 0;
                window.rightCensoredEpisodeCount += rightCensored ? 1 : 0;
                if (!leftCensored && !rightCensored)
                {
                    ++window.completeEpisodeCount;
                }
            }
            windows.push_back(window);
        }
        return windows;
    }

    /** Write sorted episode evidence without any per-second risk trace. */
    void WriteEpisodes(const std::filesystem::path& path) const
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("cannot write " + path.string());
        }
        output << "episode_id,node_id,entry_time_ns,exit_time_ns,"
                  "exposure_duration_ns,left_censored,right_censored\n";
        output << std::boolalpha;
        for (const ExposureEpisode& episode : m_episodes)
        {
            output << episode.episodeId << ',' << episode.nodeId << ','
                   << episode.entryTimeNs << ',';
            if (episode.exitTimeNs.has_value())
            {
                output << episode.exitTimeNs.value();
            }
            output << ',' << episode.exposureDurationNs << ','
                   << episode.leftCensored << ',' << episode.rightCensored << '\n';
        }
    }

    const OnlineOrbitConstellation& m_constellation; ///< Shared native orbit source.
    F2RadiationFaultModel m_model; ///< Shared pure F2 conversion and exposure model.
    F2FaultParameters m_parameters; ///< Region provenance.
    int64_t m_calibrationDurationSeconds{}; ///< Total scan duration.
    int64_t m_windowDurationSeconds{}; ///< Sliding functional-window duration.
    std::vector<NodeExposureState> m_nodes; ///< Stable-ID node states.
    std::vector<uint32_t> m_insideCounts; ///< In-region node count for each second.
    std::vector<double> m_weightedRiskSums; ///< Spatial-risk sum for each second.
    std::vector<uint32_t> m_highRiskCounts; ///< Notice-active node count per second.
    std::vector<ExposureEpisode> m_episodes; ///< Complete and censored episodes.
    std::vector<ExposureEpisode> m_highRiskEpisodes; ///< Spatial NOTICE episodes.
    std::set<uint32_t> m_enteringNodeIds; ///< Nodes observed entering after time zero.
};

} // namespace

int
main(int argc, char* argv[])
{
    FaultParameters parameters = GetDefaultFaultParameters();
    std::string constellationConfig;
    std::string outputDirectory;
    int64_t calibrationDurationSeconds = 7200;
    int64_t windowDurationSeconds = 1000;
    double functionalTargetMeanFaultCount = 2.0;
    double sigmaLongitudeDegrees = parameters.f2.sigmaLongitudeDegrees;
    double sigmaLatitudeDegrees = parameters.f2.sigmaLatitudeDegrees;
    double spatialRiskThreshold = parameters.f2.spatialRiskThreshold;
    double referenceMaximumFailureIntensity = -1.0;
    CommandLine command(__FILE__);
    command.AddValue("constellationConfig",
                     "Native ns-3.48 constellation CSV",
                     constellationConfig);
    command.AddValue("calibrationDuration",
                     "Orbit-only calibration duration in seconds",
                     calibrationDurationSeconds);
    command.AddValue("windowDuration",
                     "Sliding functional-window duration in seconds",
                     windowDurationSeconds);
    command.AddValue("targetMeanFaultCount",
                     "Target mean F2 faults in the selected functional window",
                     functionalTargetMeanFaultCount);
    command.AddValue("sigmaLongitude",
                     "Candidate Gaussian longitude sigma in degrees",
                     sigmaLongitudeDegrees);
    command.AddValue("sigmaLatitude",
                     "Candidate Gaussian latitude sigma in degrees",
                     sigmaLatitudeDegrees);
    command.AddValue("spatialRiskThreshold",
                     "Candidate spatial NOTICE threshold",
                     spatialRiskThreshold);
    command.AddValue("referenceMaximumFailureIntensity",
                     "Fixed hotspot effective failure intensity for scale validation; "
                     "negative disables",
                     referenceMaximumFailureIntensity);
    command.AddValue("outputDir", "Calibration output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        if (constellationConfig.empty() || outputDirectory.empty())
        {
            throw std::runtime_error("constellationConfig and outputDir are required");
        }
        if (calibrationDurationSeconds <= 0 || windowDurationSeconds <= 0 ||
            windowDurationSeconds > calibrationDurationSeconds)
        {
            throw std::runtime_error("calibration and window durations are invalid");
        }
        if (!std::isfinite(functionalTargetMeanFaultCount) ||
            functionalTargetMeanFaultCount <= 0.0)
        {
            throw std::runtime_error(
                "target mean fault count must be finite and positive");
        }
        if (!std::isfinite(referenceMaximumFailureIntensity))
        {
            throw std::runtime_error(
                "reference maximum failure intensity must be finite");
        }
        parameters.f2.sigmaLongitudeDegrees = sigmaLongitudeDegrees;
        parameters.f2.sigmaLatitudeDegrees = sigmaLatitudeDegrees;
        parameters.f2.spatialRiskThreshold = spatialRiskThreshold;
        ValidateFaultParameters(parameters);
        if (parameters.checkIntervalSeconds != 1.0)
        {
            throw std::runtime_error("F2 orbit calibration requires a 1-second interval");
        }
        const ConstellationDefinition definition =
            LoadConstellationDefinition(constellationConfig);
        OnlineOrbitConstellation constellation(definition);
        ExposureCalibration calibration(constellation,
                                        parameters.f2,
                                        calibrationDurationSeconds,
                                        windowDurationSeconds);
        calibration.Run();
        const std::optional<double> reference =
            referenceMaximumFailureIntensity >= 0.0
                ? std::optional<double>(referenceMaximumFailureIntensity)
                : std::nullopt;
        const Json summary =
            calibration.Finalize(outputDirectory,
                                 constellationConfig,
                                 functionalTargetMeanFaultCount,
                                 reference);
        Simulator::Destroy();
        std::cout << summary.dump() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
