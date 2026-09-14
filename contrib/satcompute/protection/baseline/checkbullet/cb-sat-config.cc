/* SPDX-License-Identifier: GPL-2.0-only */
#include "cb-sat-config.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace ns3::protection::checkbullet
{
CbMtbfProfile ReadMtbfProfile(std::istream& input)
{
    const auto j = nlohmann::json::parse(input);
    const auto require = [](bool ok) {
        if (!ok) throw std::invalid_argument("CB-Sat: invalid independent pooled MTBF profile");
    };
    require(j.at("scope") == "healthy_ordinary_running_primary_before_draw" &&
            j.at("clock") == "eligible_checks_times_check_interval" &&
            j.at("protection_mode") == "off" && j.at("f3_enabled") == false &&
            j.at("seed") == 1);
    const auto runs = j.at("runs").get<std::vector<unsigned>>();
    require(runs == std::vector<unsigned>({101,102,103,104,105,106,107,108,109,110}));
    CbMtbfProfile p;
    require(j.at("eligible_check_count").is_number_unsigned() &&
            j.at("joint_failure_count").is_number_unsigned() &&
            j.at("check_interval_ns").is_number_unsigned());
    p.eligibleChecks = j.at("eligible_check_count").get<uint64_t>();
    p.jointFailures = j.at("joint_failure_count").get<uint64_t>();
    p.checkIntervalNs = j.at("check_interval_ns").get<int64_t>();
    p.exposureSeconds = j.at("eligible_exposure_seconds").get<double>();
    p.source = j.at("source_execution").get<std::string>();
    require(p.eligibleChecks > 0 && p.jointFailures <= p.eligibleChecks &&
            p.checkIntervalNs > 0 && std::isfinite(p.exposureSeconds) &&
            p.exposureSeconds > 0 && !p.source.empty() &&
            std::abs(p.exposureSeconds - static_cast<double>(p.eligibleChecks) *
                p.checkIntervalNs / 1e9) <= 1e-8 * p.exposureSeconds);
    if (p.jointFailures == 0)
    {
        require(j.at("mtbf_seconds").is_null());
        p.mtbfSeconds = std::numeric_limits<double>::infinity();
    }
    else
    {
        p.mtbfSeconds = j.at("mtbf_seconds").get<double>();
        require(std::isfinite(p.mtbfSeconds) && p.mtbfSeconds > 0 &&
                std::abs(p.mtbfSeconds - p.exposureSeconds / p.jointFailures) <=
                    1e-8 * p.mtbfSeconds);
    }
    return p;
}
std::filesystem::path MtbfProfilePath()
{
    return SATCOMPUTE_CB_MTBF_PROFILE;
}
CbMtbfProfile LoadMtbfProfile()
{
    std::ifstream input(MtbfProfilePath());
    if (!input) throw std::runtime_error("CB-Sat calibration profile missing: " +
                                        MtbfProfilePath().string());
    return ReadMtbfProfile(input);
}
} // namespace ns3::protection::checkbullet
