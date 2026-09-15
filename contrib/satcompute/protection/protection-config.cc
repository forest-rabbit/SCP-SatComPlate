/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-config.h"
#include "ns3/command-line.h"
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ns3::protection
{
namespace
{
[[noreturn]] void Fail(const std::string& key, const std::string& reason)
{
    throw std::invalid_argument(key + " " + reason);
}
template<class T>
T Choice(const std::string& key, const std::string& value,
         std::initializer_list<std::pair<std::string_view, T>> choices)
{
    for (const auto& [name, result] : choices)
        if (value == name) return result;
    Fail(key, "has an unsupported value: " + value);
}
template<class T>
T Unsigned(const std::string& key, const std::string& value)
{
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        Fail(key, "must be an unsigned integer");
    return result;
}
BaselinePlacementKind BaselinePlacement(const std::string& key, const std::string& value)
{
    return Choice<BaselinePlacementKind>(key, value,
        {{"ffp", BaselinePlacementKind::FFP}, {"fa-ffp", BaselinePlacementKind::FA_FFP},
         {"lrl", BaselinePlacementKind::LRL}, {"fa-lrl", BaselinePlacementKind::FA_LRL}});
}
PlacementPolicyKind SharedPlacement(BaselinePlacementKind value)
{
    switch (value)
    {
    case BaselinePlacementKind::FFP: return PlacementPolicyKind::FFP;
    case BaselinePlacementKind::FA_FFP: return PlacementPolicyKind::FA_FFP;
    case BaselinePlacementKind::LRL: return PlacementPolicyKind::LRL;
    case BaselinePlacementKind::FA_LRL: return PlacementPolicyKind::FA_LRL;
    }
    Fail("placement", "unsupported private baseline placement");
}
RecoveryFallbackKind Fallback(const std::string& key, const std::string& value)
{
    return Choice<RecoveryFallbackKind>(key, value,
        {{"relocate", RecoveryFallbackKind::RELOCATE}, {"recompute", RecoveryFallbackKind::RECOMPUTE}});
}
bool BaselineScheme(ProtectionScheme scheme)
{
    return scheme == ProtectionScheme::RECOMPUTE || scheme == ProtectionScheme::ONE_PLUS_ONE ||
           scheme == ProtectionScheme::CB_SAT;
}
} // namespace

bool IsFixedProtection(const ProtectionConfig& c)
{ return c.scheme == ProtectionScheme::COMPFRR && c.compfrr.checkpointPolicy == CheckpointPolicyKind::FIXED; }
bool IsAdaptiveProtection(const ProtectionConfig& c)
{ return c.scheme == ProtectionScheme::COMPFRR && c.compfrr.checkpointPolicy == CheckpointPolicyKind::ADAPTIVE; }
bool UsesCheckpointPool(const ProtectionConfig& c)
{ return c.scheme == ProtectionScheme::COMPFRR || c.scheme == ProtectionScheme::CB_SAT; }

void RegisterProtectionOptions(CommandLine& command, ProtectionConfig& c,
                               ProtectionCliState& cli, bool testInterface)
{
    cli.testInterface = testInterface;
    const auto add = [&](std::string key, std::string help, auto setter) {
        command.AddValue(key, help, Callback<bool, const std::string&>(
            [key, setter, &cli](const std::string& value) {
                if (!cli.supplied.insert(key).second) Fail(key, "was supplied more than once");
                setter(value);
                return true;
            }));
    };
    add("protectionScheme", "off / compfrr / recompute / one-plus-one / cb-sat (default compfrr)",
        [&](const auto& v) {
            if (v == "multitree") Fail("protectionScheme", "multitree is not implemented");
            c.scheme = Choice<ProtectionScheme>("protectionScheme", v,
                {{"off", ProtectionScheme::OFF}, {"compfrr", ProtectionScheme::COMPFRR},
                 {"recompute", ProtectionScheme::RECOMPUTE}, {"one-plus-one", ProtectionScheme::ONE_PLUS_ONE},
                 {"cb-sat", ProtectionScheme::CB_SAT}});
        });
    add("compfrrCheckpointPolicy", "fixed / adaptive (default adaptive)", [&](const auto& v) {
        c.compfrr.checkpointPolicy = Choice<CheckpointPolicyKind>("compfrrCheckpointPolicy", v,
            {{"fixed", CheckpointPolicyKind::FIXED}, {"adaptive", CheckpointPolicyKind::ADAPTIVE}});
    });
    add("compfrrPlacementPolicy", "ffp / fa-ffp / lrl / fa-lrl / compfrr (default compfrr)", [&](const auto& v) {
        c.compfrr.placementPolicy = v == "compfrr" ? PlacementPolicyKind::COMPFRR :
            SharedPlacement(BaselinePlacement("compfrrPlacementPolicy", v));
    });
    add("compfrrInputPolicy", "eager / deferred / selective (default selective)", [&](const auto& v) {
        c.compfrr.inputPolicy = Choice<InputPolicy>("compfrrInputPolicy", v,
            {{"eager", InputPolicy::EAGER}, {"deferred", InputPolicy::DEFERRED}, {"selective", InputPolicy::SELECTIVE}});
    });
    add("compfrrRecoveryPolicy", "relocate / recompute; existing fallback branches only (default relocate)",
        [&](const auto& v) { c.compfrr.recoveryPolicy = Fallback("compfrrRecoveryPolicy", v); });
    add("compfrrPressureModel", "cumulative / idle-aware; CompFRR-P only (default cumulative)", [&](const auto& v) {
        if (v == "recent-U") Fail("compfrrPressureModel", "recent-U is historical-only");
        c.compfrr.pressureModel = Choice<ComputePressurePolicy>("compfrrPressureModel", v,
            {{"cumulative", ComputePressurePolicy::CUMULATIVE}, {"idle-aware", ComputePressurePolicy::IDLE_AWARE}});
    });
    add("compfrrPlacementAblation", "none / noR / noU / noM; cumulative only for ablations", [&](const auto& v) {
        c.compfrr.placementAblation = Choice<PlacementAblation>("compfrrPlacementAblation", v,
            {{"none", PlacementAblation::NONE}, {"noR", PlacementAblation::NO_R},
             {"noU", PlacementAblation::NO_U}, {"noM", PlacementAblation::NO_M}});
    });
    add("compfrrFixedDelta", "Fixed progress fraction; 0.05 = 5%, per-mille precision", [&](const auto& v) {
        std::size_t end{};
        try { c.compfrr.fixed.delta = std::stod(v, &end); }
        catch (...) { Fail("compfrrFixedDelta", "must be a progress fraction"); }
        if (end != v.size()) Fail("compfrrFixedDelta", "has trailing characters");
    });
    add("compfrrFixedBatchN", "Fixed remote batch record count (default 4)",
        [&](const auto& v) { c.compfrr.fixed.batchN = Unsigned<uint32_t>("compfrrFixedBatchN", v); });
    add("backupStorageBytesPerNode", "Checkpoint pool capacity in decimal bytes (default 10000000000)",
        [&](const auto& v) { c.common.backupStorageBytesPerNode = Unsigned<uint64_t>("backupStorageBytesPerNode", v); });
    add("compfrr-shadow", "Independent read-only observer; off + generate only (default 0)", [&](const auto& v) {
        c.diagnostics.compfrrShadow = Choice<bool>("compfrr-shadow", v,
            {{"1", true}, {"true", true}, {"0", false}, {"false", false}});
    });
    add("compfrr-shadow-output", "Shadow directory; empty uses outputDir/shadow",
        [&](const auto& v) { c.diagnostics.compfrrShadowOutput = v; });
    if (testInterface)
    {
        add("testBaselinePlacement", "TEST ONLY: private ffp / fa-ffp / lrl / fa-lrl",
            [&](const auto& v) { BaselinePlacement("testBaselinePlacement", v); cli.testBaselinePlacement = v; });
        add("testCbSatBusyPolicy", "TEST ONLY: private CB recompute / relocate",
            [&](const auto& v) { Fallback("testCbSatBusyPolicy", v); cli.testCbSatBusyPolicy = v; });
        add("testLrlRecoveryWeight", "TEST ONLY: preserve explicitly recorded historical weight",
            [&](const auto& v) { c.commonPlacement.lrlRecoveryWeight = Unsigned<uint32_t>("testLrlRecoveryWeight", v); });
    }
}

void ApplyProtectionTestOverrides(ProtectionConfig& c, const ProtectionCliState& cli)
{
    if (!cli.testBaselinePlacement.empty())
    {
        if (!cli.testInterface || !BaselineScheme(c.scheme))
            Fail("testBaselinePlacement", "requires a baseline in the test executable");
        const auto value = BaselinePlacement("testBaselinePlacement", cli.testBaselinePlacement);
        if (c.scheme == ProtectionScheme::RECOMPUTE) c.recompute.placementPolicy = value;
        if (c.scheme == ProtectionScheme::ONE_PLUS_ONE) c.onePlusOne.placementPolicy = value;
        if (c.scheme == ProtectionScheme::CB_SAT) c.cbSat.placementPolicy = value;
    }
    if (!cli.testCbSatBusyPolicy.empty())
    {
        if (!cli.testInterface || c.scheme != ProtectionScheme::CB_SAT)
            Fail("testCbSatBusyPolicy", "requires cb-sat in the test executable");
        c.cbSat.busyPolicy = Fallback("testCbSatBusyPolicy", cli.testCbSatBusyPolicy);
    }
}

PlacementPolicyKind ActivePlacementPolicy(const ProtectionConfig& c)
{
    if (c.scheme == ProtectionScheme::COMPFRR) return c.compfrr.placementPolicy;
    if (c.scheme == ProtectionScheme::RECOMPUTE) return SharedPlacement(c.recompute.placementPolicy);
    if (c.scheme == ProtectionScheme::ONE_PLUS_ONE) return SharedPlacement(c.onePlusOne.placementPolicy);
    if (c.scheme == ProtectionScheme::CB_SAT) return SharedPlacement(c.cbSat.placementPolicy);
    Fail("placement", "off has no placement policy");
}

uint32_t FixedDeltaPermille(const FixedCheckpointConfig& c)
{
    if (!std::isfinite(c.delta) || c.delta <= 0 || c.delta > 1 ||
        std::abs(c.delta * 1000 - std::round(c.delta * 1000)) > 1e-9)
        Fail("compfrrFixedDelta", "must be in (0,1] with per-mille precision");
    const auto delta = static_cast<uint32_t>(std::round(c.delta * 1000));
    if (!delta || !c.batchN || c.batchN > 1000 / delta)
        Fail("compfrrFixedBatchN", "requires n>0 and n*delta<=1");
    return delta;
}

std::string LegacyPlacementVariant(const CompFrrConfig& c)
{
    if (c.pressureModel == ComputePressurePolicy::IDLE_AWARE)
    {
        if (c.placementAblation != PlacementAblation::NONE)
            Fail("compfrrPlacementAblation", "idle-aware ablations are not supported");
        return "rational-U";
    }
    if (c.pressureModel != ComputePressurePolicy::CUMULATIVE)
        Fail("compfrrPressureModel", "has an unsupported enum value");
    switch (c.placementAblation)
    {
    case PlacementAblation::NONE: return "full";
    case PlacementAblation::NO_R: return "noR";
    case PlacementAblation::NO_U: return "noU";
    case PlacementAblation::NO_M: return "noM";
    }
    Fail("compfrrPlacementAblation", "has an unsupported enum value");
}

void ValidateProtectionConfig(const ProtectionConfig& c, const ProtectionCliState& cli,
                              const ProtectionValidationContext& context)
{
    if (c.scheme != ProtectionScheme::OFF && c.scheme != ProtectionScheme::COMPFRR && !BaselineScheme(c.scheme))
        Fail("protectionScheme", "has an unsupported enum value");
    for (const auto& key : cli.supplied)
    {
        if (key.rfind("compfrr", 0) == 0 && key.rfind("compfrr-", 0) != 0 && c.scheme != ProtectionScheme::COMPFRR)
            Fail(key, "requires protectionScheme=compfrr");
        if ((key == "compfrrFixedDelta" || key == "compfrrFixedBatchN") && !IsFixedProtection(c))
            Fail(key, "requires compfrrCheckpointPolicy=fixed");
        if ((key == "compfrrPressureModel" || key == "compfrrPlacementAblation") &&
            (!IsAdaptiveProtection(c) || c.compfrr.placementPolicy != PlacementPolicyKind::COMPFRR))
            Fail(key, "requires adaptive CompFRR-P placement");
        if (key == "backupStorageBytesPerNode" && !UsesCheckpointPool(c))
            Fail(key, "requires a checkpoint-pool consumer");
    }
    if (c.scheme != ProtectionScheme::OFF)
    {
        if (context.topologyOnly || !context.hasTasks || c.diagnostics.compfrrShadow)
            Fail("protectionScheme", "protection requires network tasks and shadow off");
        const auto placement = ActivePlacementPolicy(c);
        if (placement != PlacementPolicyKind::FFP && placement != PlacementPolicyKind::FA_FFP &&
            placement != PlacementPolicyKind::LRL && placement != PlacementPolicyKind::FA_LRL &&
            !(placement == PlacementPolicyKind::COMPFRR && IsAdaptiveProtection(c)))
            Fail("compfrrPlacementPolicy", "unsupported scheme/placement combination");
    }
    if (c.scheme == ProtectionScheme::COMPFRR)
    {
        if (!IsFixedProtection(c) && !IsAdaptiveProtection(c)) Fail("compfrrCheckpointPolicy", "unsupported enum value");
        if (c.compfrr.inputPolicy != InputPolicy::EAGER && c.compfrr.inputPolicy != InputPolicy::DEFERRED &&
            c.compfrr.inputPolicy != InputPolicy::SELECTIVE) Fail("compfrrInputPolicy", "unsupported enum value");
        if (IsFixedProtection(c))
        {
            if (c.compfrr.inputPolicy != InputPolicy::EAGER) Fail("compfrrInputPolicy", "fixed requires eager");
            FixedDeltaPermille(c.compfrr.fixed);
        }
        if (c.compfrr.recoveryPolicy != RecoveryFallbackKind::RECOMPUTE &&
            c.compfrr.recoveryPolicy != RecoveryFallbackKind::RELOCATE) Fail("compfrrRecoveryPolicy", "unsupported enum value");
        if (c.compfrr.placementPolicy == PlacementPolicyKind::COMPFRR) LegacyPlacementVariant(c.compfrr);
        else if (c.compfrr.pressureModel != ComputePressurePolicy::CUMULATIVE || c.compfrr.placementAblation != PlacementAblation::NONE)
            Fail("compfrrPressureModel", "non-default variants require CompFRR-P");
    }
    if (IsAdaptiveProtection(c) && (context.faultMode != "generate" || (!context.f1Enabled && !context.f2Enabled)))
        Fail("protectionScheme", "adaptive compfrr requires online faultMode=generate and F1 or F2");
    if (c.scheme == ProtectionScheme::CB_SAT && c.cbSat.busyPolicy != RecoveryFallbackKind::RECOMPUTE &&
        c.cbSat.busyPolicy != RecoveryFallbackKind::RELOCATE) Fail("cbSat.busyPolicy", "unsupported enum value");
    if (c.diagnostics.compfrrShadow &&
        (c.scheme != ProtectionScheme::OFF || context.topologyOnly || !context.hasTasks || context.faultMode != "generate"))
        Fail("compfrr-shadow", "requires off, network tasks and faultMode=generate");
    if (!c.diagnostics.compfrrShadow && !c.diagnostics.compfrrShadowOutput.empty())
        Fail("compfrr-shadow-output", "requires compfrr-shadow=1");
}
} // namespace ns3::protection
