/* SPDX-License-Identifier: GPL-2.0-only */
#include "multitree-published-rule.h"
#include <cmath>
#include <stdexcept>
namespace ns3::protection::multitree
{
const char* ToString(Decision decision)
{
    switch (decision)
    {
    case Decision::UNDECIDED: return "UNDECIDED";
    case Decision::RS: return "RS";
    case Decision::RP: return "RP";
    }
    throw std::invalid_argument("Unknown Multi-tree decision");
}
RuleResult Evaluate(const Features& f)
{
    if (!std::isfinite(f.ts) || !std::isfinite(f.iddl) || !std::isfinite(f.cl) ||
        std::isnan(f.fr) || f.ts < 1 || f.ts > 3 || f.iddl < 5 || f.iddl > 13 ||
        f.cl < 0 || f.fr < 0)
        throw std::invalid_argument("Invalid Multi-tree feature");
    if (f.iddl > 9)
    {
        if (f.cl < 4.5) return {Decision::RS, "D_HIGH_CL_LOW"};
        if (f.cl < 8.2)
        {
            if (f.ts < 2.2) return {Decision::RS, "D_HIGH_CL_MID_TS_LOW"};
            if (f.cl < 5.0) return {Decision::RS, "D_HIGH_CL_MID_TS_HIGH_CL_LOW"};
            return {Decision::RP, "D_HIGH_CL_MID_TS_HIGH_CL_HIGH"};
        }
        return {Decision::RP, "D_HIGH_CL_HIGH"};
    }
    if (f.fr > 0.3) return {Decision::RP, "D_LOW_FR_HIGH"};
    if (f.cl < 3.5) return {Decision::RS, "D_LOW_FR_LOW_CL_LOW"};
    return {Decision::RP, "D_LOW_FR_LOW_CL_HIGH"};
}
} // namespace ns3::protection::multitree
