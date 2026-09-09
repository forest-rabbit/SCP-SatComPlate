/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-shadow-recorder.h"
#include <fstream>
#include <set>
#include <stdexcept>

namespace ns3::compfrr
{
void
WriteShadowCsv(const std::filesystem::path& path,
               const std::vector<nlohmann::json>& rows,
               const std::vector<std::string>& requiredColumns)
{
    std::set<std::string> keys(requiredColumns.begin(), requiredColumns.end());
    for (const auto& row : rows)
        for (const auto& [key, value] : row.items())
            keys.insert(key);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    if (!stream)
        throw std::runtime_error("cannot write shadow output: " + path.string());
    const auto line = [&](const std::vector<std::string>& fields) {
        bool first = true;
        for (const auto& field : fields)
        {
            if (!first)
                stream << ',';
            first = false;
            stream << '"';
            for (char c : field)
            {
                if (c == '"')
                    stream << '"';
                stream << c;
            }
            stream << '"';
        }
        stream << '\n';
    };
    line({keys.begin(), keys.end()});
    for (const auto& row : rows)
    {
        std::vector<std::string> fields;
        for (const auto& key : keys)
            fields.push_back(!row.contains(key) || row[key].is_null() ? ""
                             : row[key].is_string()                   ? row[key].get<std::string>()
                                                                      : row[key].dump());
        line(fields);
    }
    if (!stream)
        throw std::runtime_error("failed while writing shadow CSV");
}
} // namespace ns3::compfrr
