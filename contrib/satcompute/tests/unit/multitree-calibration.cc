/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/multitree-feature-adapter.h"
#include "ns3/para.h"
#include "ns3/compute-profile.h"
#include "ns3/task-trace.h"
#include "ns3/command-line.h"
#include "ns3/constellation-definition.h"
#include "ns3/time-conversion.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace ns3;
using namespace ns3::protection::multitree;
namespace
{
/** File validation needs stable ID existence, not a simulated network. */
class Endpoints : public SatelliteEndpointView
{
  public:
    explicit Endpoints(uint32_t count) : m_count(count) {}
    bool HasSatelliteId(uint32_t id) const override { return id < m_count; }
    Ipv4Address GetServiceAddressBySatelliteId(uint32_t id) const override
    {
        if (!HasSatelliteId(id)) throw std::out_of_range("Calibration satellite ID");
        return Ipv4Address(0x0a000001 + id);
    }
  private:
    uint32_t m_count;
};
}
int main(int argc, char** argv)
{
    std::string output, commit;
    CommandLine command;
    command.AddValue("output", "Generated calibration artifact path", output);
    command.AddValue("generationCommit", "Source revision receipt (not an integrity hash)", commit);
    command.Parse(argc, argv);
    try
    {
        if (output.empty() || commit.empty()) throw std::invalid_argument("Specify output and generationCommit");
        const auto config = GetDefaultSatComputeConfig();
        Endpoints endpoints(LoadConstellationDefinition(config.constellationConfig).GetSatelliteCount());
        const auto profile = ReadComputeProfile(config.computeProfile, endpoints);
        const auto trace = ReadTaskTrace(config.taskTrace,
            SatComputeSecondsToNanoseconds(config.simulationDurationSeconds, "duration"), endpoints, profile);
        std::vector<uint64_t> inputs, budgets;
        for (const auto& definition : trace.tasks)
        {
            TaskRuntime task(definition);
            task.ConfigureComputeDeadline(GetComputeNodeProfile(profile, definition.computeNodeId)
                                              .computeRateWorkUnitsPerSecond,
                                          config.computeDeadlineFactor);
            inputs.push_back(definition.inputBytes);
            budgets.push_back(task.computeDeadlineBudgetNs);
        }
        const auto encode = [](const std::vector<RankKnot>& knots) {
            auto rows = nlohmann::json::array();
            for (const auto& knot : knots) rows.push_back({{"raw", knot.raw}, {"percentile", knot.percentile}});
            return rows;
        };
        nlohmann::json artifact = {{"source_task_trace", config.taskTrace},
            {"source_compute_profile", config.computeProfile}, {"task_count", trace.tasks.size()},
            {"compute_deadline_factor", config.computeDeadlineFactor}, {"generation_commit", commit},
            {"ts_mapping", "1 + 2 * tied mid-rank(input_bytes)"},
            {"iddl_mapping", "5 + 8 * tied mid-rank(exact runtime computeDeadlineBudgetNs)"},
            {"unseen_values", "linear interpolation of frozen knots; endpoint clamp"},
            {"notes", "Fixed calibration, never recomputed by runtime or per seed/run; no outcome inputs"},
            {"input_knots", encode(MakeRanks(inputs))}, {"deadline_knots", encode(MakeRanks(budgets))}};
        std::filesystem::create_directories(std::filesystem::absolute(output).parent_path());
        std::ofstream stream(output);
        stream << artifact.dump(2) << '\n';
        if (!stream) throw std::runtime_error("Cannot write calibration");
        std::cout << "calibrated " << trace.tasks.size() << " tasks\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
