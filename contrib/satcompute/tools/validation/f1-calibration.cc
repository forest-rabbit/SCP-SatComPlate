/*
 * SPDX-License-Identifier: GPL-2.0-only
 */
// Pure reference curves, not a substitute for C800 stochastic calibration.
#include "ns3/command-line.h"
#include "ns3/fault-para.h"
#include "ns3/fault-parameter-validator.h"
#include "ns3/f1-self-state-fault-model.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
using namespace ns3;
int main(int argc, char* argv[])
{
    std::string outputDirectory;
    CommandLine command(__FILE__);
    command.AddValue("outputDir", "Pure F1 reference curve output directory", outputDirectory);
    command.Parse(argc, argv);
    try
    {
        if (outputDirectory.empty())
            throw std::runtime_error("outputDir required");
        const auto root = std::filesystem::path(outputDirectory);
        std::filesystem::create_directories(root);
        std::ofstream csv(root / "n4b-f1-calibration.csv");
        if (!csv) throw std::runtime_error("cannot write F1 reference CSV");
        csv << "beta,phase,elapsed_s,temperature_c,p_f1,recovery_duration_s\n";
        csv << std::setprecision(17);
        auto parameters = GetDefaultFaultParameters();
        for (double beta : {3., 4., 5., 6.})
        {
            parameters.f1.temperature.growthFactor = beta;
            ValidateFaultParameters(parameters);
            const F1SelfStateFaultModel model(parameters.f1);
            auto state = model.CreateInitialSnapshot();
            for (int i = 0; i <= 120; ++i)
            {
                if (i) model.Update(state, true, .25);
                csv << beta << ",heating," << i * .25 << ',' << state.temperatureC << ','
                    << state.stepFailureProbability << ','
                    << model.GetRecoveryDurationSeconds(state.temperatureC) << '\n';
            }
            state.temperatureC = parameters.f1.temperature.criticalC;
            for (int i = 1; i <= 16; ++i)
            {
                model.Update(state, false, .25);
                csv << beta << ",cooling," << i * .25 << ',' << state.temperatureC << ','
                    << state.stepFailureProbability << ','
                    << model.GetRecoveryDurationSeconds(state.temperatureC) << '\n';
            }
        }
        const F1SelfStateFaultModel reference(GetDefaultFaultParameters().f1);
        const nlohmann::json summary = {
            {"scope", "pure reference; C800 pilot determines the final beta"},
            {"heating_to_critical_s", 30}, {"cooling_from_critical_to_base_s", 4},
            {"derived_heating_tau_s", reference.GetHeatingTauSeconds()},
            {"derived_cooling_rate_c_per_s", reference.GetCoolingRate()},
            {"beta_candidates", {3, 4, 5, 6}}, {"reference_probability_interval_s", 1}};
        std::ofstream json(root / "n4b-f1-calibration-summary.json");
        json << summary.dump(2) << '\n';
        if (!csv || !json) throw std::runtime_error("F1 reference output failed");
        std::cout << summary.dump() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
