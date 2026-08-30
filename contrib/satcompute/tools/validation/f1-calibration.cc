/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/fault-parameter-validator.h"
#include "ns3/fault-para.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/self-state-fault-model.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

namespace
{

using Json = nlohmann::ordered_json;

constexpr std::array<double, 3> HEATING_TAU_CANDIDATES = {39.0, 43.0, 47.0};
constexpr std::array<double, 4> COOLING_TAU_CANDIDATES = {20.0, 30.0, 40.0, 60.0};
constexpr std::array<double, 4> FAILURE_INTENSITY_CANDIDATES = {0.005, 0.01, 0.02, 0.05};
constexpr int64_t REPRESENTATIVE_TASK_SECONDS = 10;
constexpr uint32_t MONTE_CARLO_NODE_COUNT = 66;
constexpr uint32_t MONTE_CARLO_HOTSPOT_COUNT = 3;
constexpr int64_t MONTE_CARLO_DURATION_SECONDS = 1000;
constexpr int64_t MONTE_CARLO_BUSY_SECONDS = 50;
constexpr int64_t MONTE_CARLO_IDLE_SECONDS = 290;
constexpr uint32_t MONTE_CARLO_RUN_COUNT = 30;
constexpr uint32_t MONTE_CARLO_SEED = 1;
constexpr int64_t COMPUTE_FAULT_STREAM_BASE = 1000000;

/** Threshold observations for one heating-time-constant candidate. */
struct HeatingResult
{
    double tauSeconds{}; ///< Candidate heating time constant.
    std::optional<int64_t> timeToTemperatureRiskSeconds; ///< First T >= T_risk.
    std::optional<int64_t> timeToRiskThresholdSeconds;   ///< First active F1 risk.
    std::optional<int64_t> timeToCriticalSeconds;        ///< First T >= T_crit.
    double temperatureAfterTenSeconds{}; ///< Single-task temperature.
};

/** Temperature observations for one cooling-time-constant candidate. */
struct CoolingResult
{
    double tauSeconds{}; ///< Candidate cooling time constant.
    double temperatureAfterThirtySeconds{}; ///< Temperature after 30 s idle.
    double temperatureAfterSixtySeconds{};  ///< Temperature after 60 s idle.
    double temperatureAfterOneHundredTwentySeconds{}; ///< Temperature after 120 s idle.
};

/** Per-run evidence used to choose one F1 failure-intensity candidate. */
struct MonteCarloRunResult
{
    uint64_t run{};                    ///< Fixed ns-3 run number.
    uint64_t faultCount{};             ///< Sampled recoverable compute faults.
    uint64_t warnedFaultCount{};       ///< Faults after a risk notice.
    uint64_t unannouncedFaultCount{};  ///< Faults before the risk threshold.
    uint64_t riskOnlyEpisodeCount{};   ///< Risk episodes ending without a fault.
    std::vector<double> faultTemperaturesC; ///< Temperatures at sampled faults.
    std::vector<double> warningLeadTimesSeconds; ///< Notice-to-fault durations.
};

/** Aggregated fixed-seed evidence for one failure-intensity candidate. */
struct MonteCarloCandidateResult
{
    double intensityPerSecond{}; ///< Candidate maximum F1 intensity.
    std::vector<MonteCarloRunResult> runs; ///< All fixed run results.
    double meanFaultCount{}; ///< Mean faults per 66 satellites and 1000 s.
    double meanWarnedFaultCount{}; ///< Mean warned faults.
    double meanUnannouncedFaultCount{}; ///< Mean unannounced faults.
    double meanRiskOnlyEpisodeCount{}; ///< Mean risk-only episodes.
    std::vector<double> faultTemperaturesC; ///< Pooled fault temperatures.
    std::vector<double> warningLeadTimesSeconds; ///< Pooled warning lead times.
};

/** Runtime state for one pure-model Monte Carlo node. */
struct MonteCarloNodeState
{
    SelfStateFaultSnapshot snapshot; ///< Current F1 physical and risk state.
    std::optional<int64_t> noticeTimeSeconds; ///< Open risk notice time.
    bool computeAvailable{true}; ///< Recoverable compute availability.
    std::optional<int64_t> recoveryTimeSeconds; ///< End of current failure.
    Ptr<UniformRandomVariable> random; ///< Stable per-node ns-3 random stream.
};

/** Stream pure-model calibration samples into the canonical CSV. */
class CalibrationWriter
{
  public:
    /** Open and initialize a calibration CSV. */
    explicit CalibrationWriter(const std::filesystem::path& path)
        : m_output(path, std::ios::out | std::ios::trunc)
    {
        if (!m_output.is_open())
        {
            throw std::runtime_error("cannot write " + path.string());
        }
        m_output << "section,scenario,candidate_name,candidate_value,elapsed_time_s,"
                    "task_index,busy,temperature_c,depth_of_discharge,thermal_risk,"
                    "energy_pressure,combined_risk,failure_intensity_per_s,"
                    "step_failure_probability,run,fault_count,warned_fault_count,"
                    "unannounced_fault_count,risk_only_episode_count,"
                    "mean_fault_temperature_c,mean_warning_lead_time_s\n";
        m_output << std::setprecision(17) << std::boolalpha;
    }

    /**
     * Append one pure-model observation.
     *
     * @param section Candidate-grid section.
     * @param scenario Controlled utilization scenario.
     * @param candidateName Name of the varied parameter.
     * @param candidateValue Value of the varied parameter.
     * @param elapsedTimeSeconds Time since scenario start.
     * @param taskIndex Representative task index, or zero when not applicable.
     * @param snapshot Current pure F1 state.
     */
    void Write(const std::string& section,
               const std::string& scenario,
               const std::string& candidateName,
               double candidateValue,
               int64_t elapsedTimeSeconds,
               int64_t taskIndex,
               const SelfStateFaultSnapshot& snapshot)
    {
        m_output << section << ',' << scenario << ',' << candidateName << ','
                 << candidateValue << ',' << elapsedTimeSeconds << ',';
        if (taskIndex > 0)
        {
            m_output << taskIndex;
        }
        m_output << ',' << snapshot.busy << ',' << snapshot.temperatureC << ','
                 << snapshot.depthOfDischarge << ',' << snapshot.thermalRisk << ','
                 << snapshot.energyPressure << ',' << snapshot.combinedRisk << ','
                 << snapshot.failureIntensityPerSecond << ','
                 << snapshot.stepFailureProbability << ",,,,,,,\n";
    }

    /**
     * Append one fixed-seed Monte Carlo result.
     *
     * @param candidate Failure-intensity candidate.
     * @param result Per-run count and distribution evidence.
     */
    void WriteMonteCarlo(double candidate, const MonteCarloRunResult& result)
    {
        m_output << "failure_intensity_search,representative_66sat_1000s,"
                    "max_failure_intensity_per_s,"
                 << candidate << ",,,,,,,,,," << result.run << ','
                 << result.faultCount << ',' << result.warnedFaultCount << ','
                 << result.unannouncedFaultCount << ','
                 << result.riskOnlyEpisodeCount << ',';
        if (!result.faultTemperaturesC.empty())
        {
            m_output << Mean(result.faultTemperaturesC);
        }
        m_output << ',';
        if (!result.warningLeadTimesSeconds.empty())
        {
            m_output << Mean(result.warningLeadTimesSeconds);
        }
        m_output << '\n';
    }

  private:
    static double Mean(const std::vector<double>& values)
    {
        return std::accumulate(values.begin(), values.end(), 0.0) /
               static_cast<double>(values.size());
    }

    std::ofstream m_output; ///< Truncating CSV output stream.
};

Json
OptionalTime(const std::optional<int64_t>& value)
{
    return value.has_value() ? Json(value.value()) : Json(nullptr);
}

void
UpdateThresholdTimes(const F1FaultParameters& parameters,
                     const SelfStateFaultModel& model,
                     const SelfStateFaultSnapshot& snapshot,
                     int64_t elapsedTimeSeconds,
                     HeatingResult& result)
{
    if (!result.timeToTemperatureRiskSeconds.has_value() &&
        snapshot.temperatureC >= parameters.temperature.riskC)
    {
        result.timeToTemperatureRiskSeconds = elapsedTimeSeconds;
    }
    if (!result.timeToRiskThresholdSeconds.has_value() &&
        model.IsRiskActive(snapshot))
    {
        result.timeToRiskThresholdSeconds = elapsedTimeSeconds;
    }
    if (!result.timeToCriticalSeconds.has_value() &&
        snapshot.temperatureC >= parameters.temperature.criticalC)
    {
        result.timeToCriticalSeconds = elapsedTimeSeconds;
    }
}

HeatingResult
RunHeatingCandidate(const F1FaultParameters& selectedParameters,
                    double tauSeconds,
                    CalibrationWriter& writer)
{
    F1FaultParameters parameters = selectedParameters;
    parameters.temperature.heatingTauSeconds = tauSeconds;
    const SelfStateFaultModel model(parameters);
    SelfStateFaultSnapshot snapshot = model.CreateInitialSnapshot();
    HeatingResult result;
    result.tauSeconds = tauSeconds;
    for (int64_t second = 1; second <= 120; ++second)
    {
        model.Update(snapshot, true, 1.0);
        writer.Write("heating_tau_search",
                     "continuous_load",
                     "heating_tau_s",
                     tauSeconds,
                     second,
                     (second - 1) / REPRESENTATIVE_TASK_SECONDS + 1,
                     snapshot);
        UpdateThresholdTimes(parameters, model, snapshot, second, result);
        if (second == 10)
        {
            result.temperatureAfterTenSeconds = snapshot.temperatureC;
        }
    }
    return result;
}

CoolingResult
RunCoolingCandidate(const F1FaultParameters& selectedParameters,
                    double tauSeconds,
                    CalibrationWriter& writer)
{
    F1FaultParameters parameters = selectedParameters;
    parameters.temperature.coolingTauSeconds = tauSeconds;
    const SelfStateFaultModel model(parameters);
    SelfStateFaultSnapshot snapshot = model.CreateInitialSnapshot();
    snapshot.temperatureC = parameters.temperature.criticalC;
    CoolingResult result;
    result.tauSeconds = tauSeconds;
    for (int64_t second = 1; second <= 120; ++second)
    {
        model.Update(snapshot, false, 1.0);
        writer.Write("cooling_tau_search",
                     "cool_from_critical",
                     "cooling_tau_s",
                     tauSeconds,
                     second,
                     0,
                     snapshot);
        if (second == 30)
        {
            result.temperatureAfterThirtySeconds = snapshot.temperatureC;
        }
        else if (second == 60)
        {
            result.temperatureAfterSixtySeconds = snapshot.temperatureC;
        }
        else if (second == 120)
        {
            result.temperatureAfterOneHundredTwentySeconds = snapshot.temperatureC;
        }
    }
    return result;
}

Json
MakeHeatingJson(const HeatingResult& result)
{
    return {{"heating_tau_s", result.tauSeconds},
            {"temperature_after_10s_c", result.temperatureAfterTenSeconds},
            {"time_to_temperature_risk_s",
             OptionalTime(result.timeToTemperatureRiskSeconds)},
            {"time_to_risk_threshold_s",
             OptionalTime(result.timeToRiskThresholdSeconds)},
            {"time_to_critical_s", OptionalTime(result.timeToCriticalSeconds)}};
}

Json
MakeCoolingJson(const CoolingResult& result)
{
    return {{"cooling_tau_s", result.tauSeconds},
            {"temperature_after_30s_c", result.temperatureAfterThirtySeconds},
            {"temperature_after_60s_c", result.temperatureAfterSixtySeconds},
            {"temperature_after_120s_c",
             result.temperatureAfterOneHundredTwentySeconds}};
}

double
MeanCounts(const std::vector<MonteCarloRunResult>& runs,
           uint64_t MonteCarloRunResult::*member)
{
    uint64_t total = 0;
    for (const MonteCarloRunResult& run : runs)
    {
        total += run.*member;
    }
    return static_cast<double>(total) / static_cast<double>(runs.size());
}

bool
IsRepresentativeBusy(uint32_t nodeId, int64_t simulationSecond)
{
    if (nodeId < MONTE_CARLO_HOTSPOT_COUNT)
    {
        const int64_t cycleSeconds =
            MONTE_CARLO_BUSY_SECONDS + MONTE_CARLO_IDLE_SECONDS;
        return (simulationSecond - 1) % cycleSeconds < MONTE_CARLO_BUSY_SECONDS;
    }
    const int64_t sparseStart =
        100 + static_cast<int64_t>(nodeId - MONTE_CARLO_HOTSPOT_COUNT) * 10;
    return simulationSecond > sparseStart &&
           simulationSecond <= sparseStart + REPRESENTATIVE_TASK_SECONDS;
}

MonteCarloRunResult
RunMonteCarlo(const FaultParameters& sourceParameters,
              double intensityPerSecond,
              uint64_t runNumber)
{
    FaultParameters parameters = sourceParameters;
    parameters.f1.maxFailureIntensityPerSecond = intensityPerSecond;
    const SelfStateFaultModel model(parameters.f1);
    const int64_t recoverySeconds = static_cast<int64_t>(
        parameters.recoverableComputeDurationSeconds /
        parameters.checkIntervalSeconds);
    RngSeedManager::SetSeed(MONTE_CARLO_SEED);
    RngSeedManager::SetRun(runNumber);

    std::vector<MonteCarloNodeState> nodes(MONTE_CARLO_NODE_COUNT);
    for (uint32_t nodeId = 0; nodeId < nodes.size(); ++nodeId)
    {
        nodes[nodeId].snapshot = model.CreateInitialSnapshot();
        nodes[nodeId].random = CreateObject<UniformRandomVariable>();
        nodes[nodeId].random->SetStream(COMPUTE_FAULT_STREAM_BASE + nodeId);
    }

    MonteCarloRunResult result;
    result.run = runNumber;
    for (int64_t second = 1; second <= MONTE_CARLO_DURATION_SECONDS; ++second)
    {
        for (uint32_t nodeId = 0; nodeId < nodes.size(); ++nodeId)
        {
            MonteCarloNodeState& node = nodes[nodeId];
            const bool busy = node.computeAvailable &&
                              IsRepresentativeBusy(nodeId, second);
            model.Update(node.snapshot, busy, 1.0);
            if (!node.computeAvailable)
            {
                if (node.recoveryTimeSeconds == second)
                {
                    node.computeAvailable = true;
                    node.recoveryTimeSeconds = std::nullopt;
                }
                continue;
            }

            const bool riskActive = model.IsRiskActive(node.snapshot);
            if (riskActive && !node.noticeTimeSeconds.has_value())
            {
                node.noticeTimeSeconds = second;
            }
            else if (!riskActive && node.noticeTimeSeconds.has_value())
            {
                ++result.riskOnlyEpisodeCount;
                node.noticeTimeSeconds = std::nullopt;
            }

            const double randomValue = node.random->GetValue();
            if (randomValue < node.snapshot.stepFailureProbability)
            {
                ++result.faultCount;
                result.faultTemperaturesC.push_back(node.snapshot.temperatureC);
                if (node.noticeTimeSeconds.has_value())
                {
                    ++result.warnedFaultCount;
                    result.warningLeadTimesSeconds.push_back(
                        static_cast<double>(second - node.noticeTimeSeconds.value()));
                }
                else
                {
                    ++result.unannouncedFaultCount;
                }
                node.noticeTimeSeconds = std::nullopt;
                node.computeAvailable = false;
                node.recoveryTimeSeconds = second + recoverySeconds;
            }
        }
    }
    for (MonteCarloNodeState& node : nodes)
    {
        if (node.noticeTimeSeconds.has_value())
        {
            ++result.riskOnlyEpisodeCount;
        }
        node.random = nullptr;
    }
    return result;
}

MonteCarloCandidateResult
RunMonteCarloCandidate(const FaultParameters& parameters,
                       double intensityPerSecond,
                       CalibrationWriter& writer)
{
    MonteCarloCandidateResult result;
    result.intensityPerSecond = intensityPerSecond;
    result.runs.reserve(MONTE_CARLO_RUN_COUNT);
    for (uint64_t run = 1; run <= MONTE_CARLO_RUN_COUNT; ++run)
    {
        MonteCarloRunResult runResult =
            RunMonteCarlo(parameters, intensityPerSecond, run);
        writer.WriteMonteCarlo(intensityPerSecond, runResult);
        result.faultTemperaturesC.insert(result.faultTemperaturesC.end(),
                                         runResult.faultTemperaturesC.begin(),
                                         runResult.faultTemperaturesC.end());
        result.warningLeadTimesSeconds.insert(
            result.warningLeadTimesSeconds.end(),
            runResult.warningLeadTimesSeconds.begin(),
            runResult.warningLeadTimesSeconds.end());
        result.runs.push_back(std::move(runResult));
    }
    result.meanFaultCount = MeanCounts(result.runs, &MonteCarloRunResult::faultCount);
    result.meanWarnedFaultCount =
        MeanCounts(result.runs, &MonteCarloRunResult::warnedFaultCount);
    result.meanUnannouncedFaultCount =
        MeanCounts(result.runs, &MonteCarloRunResult::unannouncedFaultCount);
    result.meanRiskOnlyEpisodeCount =
        MeanCounts(result.runs, &MonteCarloRunResult::riskOnlyEpisodeCount);
    return result;
}

Json
DistributionJson(std::vector<double> values)
{
    if (values.empty())
    {
        return nullptr;
    }
    std::sort(values.begin(), values.end());
    const auto percentile = [&values](double probability) {
        const std::size_t index = static_cast<std::size_t>(
            std::floor(probability * static_cast<double>(values.size() - 1)));
        return values[index];
    };
    return {{"sample_count", values.size()},
            {"minimum", values.front()},
            {"mean",
             std::accumulate(values.begin(), values.end(), 0.0) /
                 static_cast<double>(values.size())},
            {"p50", percentile(0.50)},
            {"p95", percentile(0.95)},
            {"maximum", values.back()}};
}

Json
MakeMonteCarloJson(const MonteCarloCandidateResult& result)
{
    return {{"max_failure_intensity_per_s", result.intensityPerSecond},
            {"mean_fault_count", result.meanFaultCount},
            {"mean_warned_fault_count", result.meanWarnedFaultCount},
            {"mean_unannounced_fault_count", result.meanUnannouncedFaultCount},
            {"mean_risk_only_episode_count", result.meanRiskOnlyEpisodeCount},
            {"fault_temperature_c", DistributionJson(result.faultTemperaturesC)},
            {"warning_lead_time_s",
             DistributionJson(result.warningLeadTimesSeconds)}};
}

void
ValidateConfig(const FaultParameters& parameters)
{
    ValidateFaultParameters(parameters);
    if (!parameters.f1.enabled)
    {
        throw std::runtime_error("F1 calibration requires self_state.enabled=true");
    }
    if (parameters.f2.enabled || parameters.f3.enabled)
    {
        throw std::runtime_error("F1 calibration requires F2 and F3 to be disabled");
    }
    if (parameters.checkIntervalSeconds != 1.0)
    {
        throw std::runtime_error("current F1 calibration requires a 1-second check interval");
    }
    if (std::fmod(parameters.recoverableComputeDurationSeconds,
                  parameters.checkIntervalSeconds) != 0.0)
    {
        throw std::runtime_error(
            "F1 calibration requires recovery duration aligned to the check interval");
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Calibration output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        if (outputDirectory.empty())
        {
            throw std::runtime_error("outputDir is required");
        }
        const FaultParameters parameters = GetDefaultFaultParameters();
        ValidateConfig(parameters);
        const std::filesystem::path outputRoot(outputDirectory);
        std::filesystem::create_directories(outputRoot);
        CalibrationWriter writer(outputRoot / "n4b-f1-calibration.csv");

        std::vector<HeatingResult> heatingResults;
        for (const double candidate : HEATING_TAU_CANDIDATES)
        {
            heatingResults.push_back(
                RunHeatingCandidate(parameters.f1, candidate, writer));
        }
        std::vector<CoolingResult> coolingResults;
        for (const double candidate : COOLING_TAU_CANDIDATES)
        {
            coolingResults.push_back(
                RunCoolingCandidate(parameters.f1, candidate, writer));
        }
        std::vector<MonteCarloCandidateResult> monteCarloResults;
        for (const double candidate : FAILURE_INTENSITY_CANDIDATES)
        {
            monteCarloResults.push_back(
                RunMonteCarloCandidate(parameters, candidate, writer));
        }

        const auto selectedHeating = std::find_if(
            heatingResults.begin(),
            heatingResults.end(),
            [&parameters](const HeatingResult& result) {
                return result.tauSeconds ==
                       parameters.f1.temperature.heatingTauSeconds;
            });
        const auto selectedCooling = std::find_if(
            coolingResults.begin(),
            coolingResults.end(),
            [&parameters](const CoolingResult& result) {
                return result.tauSeconds ==
                       parameters.f1.temperature.coolingTauSeconds;
            });
        if (selectedHeating == heatingResults.end() ||
            selectedCooling == coolingResults.end())
        {
            throw std::runtime_error(
                "selected heating/cooling tau must be present in the calibration grid");
        }
        if (!selectedHeating->timeToCriticalSeconds.has_value() ||
            selectedHeating->timeToCriticalSeconds.value() < 50 ||
            selectedHeating->timeToCriticalSeconds.value() > 60)
        {
            throw std::runtime_error(
                "selected heating tau misses the current 50--60 second target");
        }
        const auto selectedIntensity = std::min_element(
            monteCarloResults.begin(),
            monteCarloResults.end(),
            [](const MonteCarloCandidateResult& left,
               const MonteCarloCandidateResult& right) {
                return std::make_pair(std::abs(left.meanFaultCount - 1.0),
                                      left.intensityPerSecond) <
                       std::make_pair(std::abs(right.meanFaultCount - 1.0),
                                      right.intensityPerSecond);
            });

        Json heating = Json::array();
        for (const HeatingResult& result : heatingResults)
        {
            heating.push_back(MakeHeatingJson(result));
        }
        Json cooling = Json::array();
        for (const CoolingResult& result : coolingResults)
        {
            cooling.push_back(MakeCoolingJson(result));
        }
        Json monteCarlo = Json::array();
        for (const MonteCarloCandidateResult& result : monteCarloResults)
        {
            monteCarlo.push_back(MakeMonteCarloJson(result));
        }
        const int64_t tasksBeforeCritical =
            (selectedHeating->timeToCriticalSeconds.value() +
             REPRESENTATIVE_TASK_SECONDS - 1) /
            REPRESENTATIVE_TASK_SECONDS;
        const SelfStateFaultModel selectedModel(parameters.f1);
        SelfStateFaultSnapshot recovery = selectedModel.CreateInitialSnapshot();
        recovery.temperatureC = parameters.f1.temperature.criticalC;
        selectedModel.Update(recovery,
                             false,
                             parameters.recoverableComputeDurationSeconds);
        const Json summary = {
            {"check_interval_s", parameters.checkIntervalSeconds},
            {"recoverable_compute_duration_s",
             parameters.recoverableComputeDurationSeconds},
            {"calibration_scope",
             "accelerated functional scenario; not a physical satellite failure rate"},
            {"heating_tau_candidates", heating},
            {"cooling_tau_candidates", cooling},
            {"monte_carlo",
             {{"random_seed", MONTE_CARLO_SEED},
              {"run_start", 1},
              {"run_count", MONTE_CARLO_RUN_COUNT},
              {"satellite_count", MONTE_CARLO_NODE_COUNT},
              {"duration_s", MONTE_CARLO_DURATION_SECONDS},
              {"hotspot_count", MONTE_CARLO_HOTSPOT_COUNT},
              {"hotspot_busy_s", MONTE_CARLO_BUSY_SECONDS},
              {"hotspot_idle_s", MONTE_CARLO_IDLE_SECONDS},
              {"functional_target_mean_fault_count", 1.0},
              {"candidates", monteCarlo}}},
            {"selected",
             {{"heating_tau_s", parameters.f1.temperature.heatingTauSeconds},
              {"cooling_tau_s", parameters.f1.temperature.coolingTauSeconds},
              {"temperature_after_recovery_from_critical_c",
               recovery.temperatureC},
              {"time_to_temperature_risk_s",
               OptionalTime(selectedHeating->timeToTemperatureRiskSeconds)},
              {"time_to_risk_threshold_s",
               OptionalTime(selectedHeating->timeToRiskThresholdSeconds)},
              {"time_to_critical_s",
               OptionalTime(selectedHeating->timeToCriticalSeconds)},
              {"representative_task_duration_s", REPRESENTATIVE_TASK_SECONDS},
              {"representative_tasks_before_critical", tasksBeforeCritical},
              {"max_failure_intensity_per_s",
               selectedIntensity->intensityPerSecond},
              {"configured_max_failure_intensity_per_s",
               parameters.f1.maxFailureIntensityPerSecond},
              {"monte_carlo_mean_fault_count", selectedIntensity->meanFaultCount},
              {"monte_carlo_mean_risk_only_episode_count",
               selectedIntensity->meanRiskOnlyEpisodeCount}}}};
        std::ofstream summaryOutput(outputRoot / "n4b-f1-calibration-summary.json",
                                    std::ios::out | std::ios::trunc);
        if (!summaryOutput.is_open())
        {
            throw std::runtime_error("cannot write F1 calibration summary");
        }
        summaryOutput << summary.dump(2) << '\n';
        std::cout << summary.dump() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
