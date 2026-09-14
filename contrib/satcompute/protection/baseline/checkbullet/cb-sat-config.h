/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_CB_SAT_CONFIG_H
#define SATCOMPUTE_CB_SAT_CONFIG_H
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>

namespace ns3::protection::checkbullet
{
/** Independent calibration evidence, not a second scenario configuration. */
struct CbMtbfProfile
{
    double mtbfSeconds{}, exposureSeconds{};
    uint64_t eligibleChecks{}, jointFailures{};
    int64_t checkIntervalNs{};
    std::string source;
};
/** Validate the production calibration scope and pooled E/N, including zero events. */
CbMtbfProfile ReadMtbfProfile(std::istream& input);
/** Only the checked-in baseline calibration location is a production parameter source. */
std::filesystem::path MtbfProfilePath();
CbMtbfProfile LoadMtbfProfile();
} // namespace ns3::protection::checkbullet
#endif
