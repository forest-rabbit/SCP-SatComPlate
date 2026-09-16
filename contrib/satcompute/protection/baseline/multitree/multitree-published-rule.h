/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_MULTITREE_PUBLISHED_RULE_H
#define SATCOMPUTE_MULTITREE_PUBLISHED_RULE_H
#include "multitree-feature-adapter.h"
namespace ns3::protection::multitree
{
/** Third FT tree of published Fig.14, not the complete trained MTGP scheduler. */
enum class Decision { UNDECIDED, RS, RP };
/** Stable leaf identity permits auditing every strict published comparison. */
struct RuleResult
{
    Decision decision{Decision::UNDECIDED}; ///< One immutable primary-start decision.
    const char* branch{}; ///< Published leaf identity.
};
const char* ToString(Decision decision);
/** Exact published thresholds and strict comparisons; no tuning/configuration. */
RuleResult Evaluate(const Features& features);
} // namespace ns3::protection::multitree
#endif
