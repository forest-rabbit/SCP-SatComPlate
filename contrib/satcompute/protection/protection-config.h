/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_CONFIG_H
#define SATCOMPUTE_PROTECTION_CONFIG_H
#include "protection-para.h"
#include <set>
#include <string>

namespace ns3 { class CommandLine; }
namespace ns3::protection
{
/** CLI provenance and test-only overrides, never simulation state. */
struct ProtectionCliState
{
    std::set<std::string> supplied;
    std::string testBaselinePlacement, testCbSatBusyPolicy;
    bool testInterface{};
};
struct ProtectionValidationContext
{
    bool topologyOnly{}, hasTasks{}, f1Enabled{}, f2Enabled{};
    std::string faultMode;
};
/** Register only scoped production controls; private overrides require a test executable. */
void RegisterProtectionOptions(CommandLine& command, ProtectionConfig& config,
                               ProtectionCliState& cli, bool testInterface = false);
/** Apply private overrides after parsing so argv order never selects a different owner. */
void ApplyProtectionTestOverrides(ProtectionConfig& config, const ProtectionCliState& cli);
/** Validate the capability matrix before creating any simulation resources. */
void ValidateProtectionConfig(const ProtectionConfig& config, const ProtectionCliState& cli,
                              const ProtectionValidationContext& context);
bool IsFixedProtection(const ProtectionConfig& config);
bool IsAdaptiveProtection(const ProtectionConfig& config);
bool UsesCheckpointPool(const ProtectionConfig& config);
PlacementPolicyKind ActivePlacementPolicy(const ProtectionConfig& config);
/** Preserve old metric/fixture variant names only at the existing policy boundary. */
std::string LegacyPlacementVariant(const CompFrrConfig& config);
uint32_t FixedDeltaPermille(const FixedCheckpointConfig& config);
} // namespace ns3::protection
#endif
