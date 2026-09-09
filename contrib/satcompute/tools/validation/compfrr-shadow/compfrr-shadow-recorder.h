/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_SHADOW_RECORDER_H
#define SATCOMPUTE_COMPFRR_SHADOW_RECORDER_H
#include <filesystem>
#include <nlohmann/json.hpp>
#include <vector>

namespace ns3::compfrr
{
/** Write stable CSV with quoted strings and empty N/A fields; creates no model state. */
void WriteShadowCsv(const std::filesystem::path& path,
                    const std::vector<nlohmann::json>& rows,
                    const std::vector<std::string>& requiredColumns);
} // namespace ns3::compfrr
#endif
