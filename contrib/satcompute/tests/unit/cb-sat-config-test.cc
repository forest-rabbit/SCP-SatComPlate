/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/cb-sat-config.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace ns3::protection::checkbullet;
int main()
{
    try
    {
        nlohmann::json valid = {{"scope", "healthy_ordinary_running_primary_before_draw"},
            {"clock", "eligible_checks_times_check_interval"}, {"protection_mode", "off"},
            {"f3_enabled", false}, {"seed", 1}, {"runs", {101,102,103,104,105,106,107,108,109,110}},
            {"eligible_check_count", 100}, {"joint_failure_count", 2}, {"check_interval_ns", 1000000000},
            {"eligible_exposure_seconds", 100.0}, {"mtbf_seconds", 50.0},
            {"source_execution", "unit-test-only-independent-calibration"}};
        const auto read = [](const auto& j) { std::istringstream input(j.dump()); return ReadMtbfProfile(input); };
        if (read(valid).mtbfSeconds != 50) throw std::runtime_error("pooled MTBF wrong");
        auto zero = valid; zero["joint_failure_count"] = 0; zero["mtbf_seconds"] = nullptr;
        if (!std::isinf(read(zero).mtbfSeconds)) throw std::runtime_error("zero-event MTBF not infinite");
        unsigned rejected{};
        const auto reject = [&](auto j) {
            try { read(j); } catch (const std::exception&) { ++rejected; return; }
            throw std::runtime_error("invalid production calibration accepted");
        };
        auto bad = valid; bad["runs"][0] = 11; reject(bad);
        bad = valid; bad["scope"] = "all_satellites_wall_time"; reject(bad);
        bad = valid; bad["protection_mode"] = "checkbullet"; reject(bad);
        bad = valid; bad["f3_enabled"] = true; reject(bad);
        bad = valid; bad["eligible_check_count"] = 0; reject(bad);
        bad = valid; bad["joint_failure_count"] = 101; reject(bad);
        bad = valid; bad["eligible_exposure_seconds"] = 1300; reject(bad);
        bad = valid; bad["check_interval_ns"] = -1; reject(bad);
        bad = valid; bad["mtbf_seconds"] = 100; reject(bad);
        bad = valid; bad["mtbf_seconds"] = nullptr; reject(bad);
        bad = zero; bad["mtbf_seconds"] = 50; reject(bad);
        bad = valid; bad["source_execution"] = ""; reject(bad);
        std::cout << "CB-Sat profile: 2 valid and " << rejected << " rejected contracts passed\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
