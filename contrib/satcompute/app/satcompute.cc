/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/satcompute-version.h"
#include "ns3/scenario-config.h"

#include "../third-party/nlohmann/json.hpp"

#include <filesystem>
#include <iostream>
#include <string>

using namespace ns3;

int
main(int argc, char* argv[])
{
    std::string scenarioConfig;
    std::string outputDirectory = "/tmp/satcompute-output";
    CommandLine command(__FILE__);
    command.AddValue("scenarioConfig", "Path to authoritative scenario 0.2 JSON", scenarioConfig);
    command.AddValue("outputDir", "Operational output directory", outputDirectory);
    command.Parse(argc, argv);

    try
    {
        if (scenarioConfig.empty())
        {
            std::cout << "{\"application\":\"satcompute\",\"scenario_schema_version\":\""
                      << GetSatComputeSchemaVersion() << "\",\"status\":\"ready\"}" << std::endl;
            return 0;
        }

        const ScenarioConfig config = LoadScenarioConfig(scenarioConfig);
        const std::filesystem::path effectiveConfig =
            WriteEffectiveConfig(config, outputDirectory);
        const nlohmann::json result = {{"application", "satcompute"},
                                       {"effective_config", effectiveConfig.string()},
                                       {"satellite_count",
                                        config.constellation.GetSatelliteCount()},
                                       {"scenario", config.scenarioName},
                                       {"status", "validated"}};
        std::cout << result.dump() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 2;
    }
}
