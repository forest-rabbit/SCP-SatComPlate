/* SPDX-License-Identifier: GPL-2.0-only */
#include "ns3/protection-config.h"
#include "ns3/command-line.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ns3;
using namespace ns3::protection;
namespace
{
unsigned checks{};
void Check(bool value) { ++checks; if (!value) throw std::runtime_error("config assertion"); }
ProtectionConfig Parse(std::vector<std::string> args, bool test = false,
                       std::string fault = "generate", bool f1 = true, bool f2 = true)
{
    // Domain/Fixed tests explicitly use its existing legal private combination.
    if (std::find(args.begin(), args.end(), "--compfrrCheckpointPolicy=fixed") != args.end())
    {
        const auto missing = [&](const std::string& prefix) {
            return std::none_of(args.begin(), args.end(), [&](const auto& a) { return a.rfind(prefix, 0) == 0; });
        };
        if (missing("--compfrrPlacementPolicy=")) args.push_back("--compfrrPlacementPolicy=fa-ffp");
        if (missing("--compfrrInputPolicy=")) args.push_back("--compfrrInputPolicy=eager");
    }
    auto c = GetDefaultProtectionConfig();
    ProtectionCliState cli;
    CommandLine command;
    RegisterProtectionOptions(command, c, cli, test);
    args.insert(args.begin(), "config-test");
    command.Parse(args);
    ApplyProtectionTestOverrides(c, cli);
    ValidateProtectionConfig(c, cli, {false, true, f1, f2, fault});
    return c;
}
template<class F> void Reject(F f)
{
    try { f(); } catch (const std::invalid_argument&) { ++checks; return; }
    throw std::runtime_error("accepted invalid capability combination");
}
}
int main()
{
    try
    {
        const auto d = Parse({});
        Check(d.scheme == ProtectionScheme::COMPFRR && d.common.backupStorageBytesPerNode == 10000000000ULL);
        Check(d.compfrr.inputPolicy == InputPolicy::SELECTIVE && d.compfrr.placementPolicy == PlacementPolicyKind::COMPFRR);
        Check(d.compfrr.recoveryPolicy == RecoveryFallbackKind::RELOCATE && d.commonPlacement.lrlRecoveryWeight == 1);
        Check(d.cbSat.busyPolicy == RecoveryFallbackKind::RECOMPUTE);
        Check(FixedDeltaPermille(d.compfrr.fixed) == 50 && d.compfrr.fixed.batchN == 4);
        for (const auto* placement : {"ffp", "fa-ffp", "lrl", "fa-lrl", "compfrr"})
            for (const auto* input : {"eager", "deferred", "selective"})
                for (const auto* recovery : {"recompute", "relocate"})
                {
                    const auto c = Parse({"--protectionScheme=compfrr", std::string("--compfrrPlacementPolicy=")+placement,
                        std::string("--compfrrInputPolicy=")+input, std::string("--compfrrRecoveryPolicy=")+recovery});
                    Check(IsAdaptiveProtection(c));
                }
        for (const auto* fault : {"none", "generate", "validation-replay"})
            for (const auto* placement : {"ffp", "fa-ffp", "lrl", "fa-lrl"})
            {
                const auto c = Parse({"--protectionScheme=compfrr", "--compfrrCheckpointPolicy=fixed",
                    std::string("--compfrrPlacementPolicy=")+placement}, false, fault, false, false);
                Check(IsFixedProtection(c));
            }
        for (const auto* scheme : {"recompute", "one-plus-one", "cb-sat"})
            for (const auto* placement : {"ffp", "fa-ffp", "lrl", "fa-lrl"})
                for (const auto* fault : {"none", "generate", "validation-replay"})
                {
                    // Deliberately put the private override before scheme to prove order independence.
                    auto c = Parse({std::string("--testBaselinePlacement=")+placement,
                        std::string("--protectionScheme=")+scheme}, true, fault, false, false);
                    const auto selected = ActivePlacementPolicy(c);
                    c.compfrr.inputPolicy = InputPolicy::SELECTIVE;
                    c.compfrr.placementPolicy = PlacementPolicyKind::COMPFRR;
                    c.compfrr.pressureModel = ComputePressurePolicy::IDLE_AWARE;
                    c.compfrr.placementAblation = PlacementAblation::NO_M;
                    c.compfrr.fixed.delta = 0;
                    ValidateProtectionConfig(c, {}, {false, true, false, false, fault});
                    Check(ActivePlacementPolicy(c) == selected);
                }
        for (const auto* busy : {"relocate", "recompute"})
            Check(Parse({std::string("--testCbSatBusyPolicy=")+busy, "--protectionScheme=cb-sat"}, true).scheme == ProtectionScheme::CB_SAT);
        for (const auto* ablation : {"none", "noR", "noU", "noM"})
        {
            auto c = Parse({"--protectionScheme=compfrr", "--compfrrPlacementPolicy=compfrr",
                std::string("--compfrrPlacementAblation=")+ablation});
            Check(LegacyPlacementVariant(c.compfrr) == (std::string(ablation) == "none" ? "full" : ablation));
        }
        Check(LegacyPlacementVariant(Parse({"--protectionScheme=compfrr", "--compfrrPlacementPolicy=compfrr",
            "--compfrrPressureModel=idle-aware"}).compfrr) == "rational-U");
        Check(Parse({"--protectionScheme=off", "--compfrr-shadow=1"}).diagnostics.compfrrShadow);
        for (const auto* scheme : {"off", "recompute", "one-plus-one", "cb-sat"})
            Reject([&] { Parse({std::string("--protectionScheme=")+scheme, "--compfrrInputPolicy=eager"}); });
        for (const auto* input : {"deferred", "selective"})
            Reject([&] { Parse({"--protectionScheme=compfrr", "--compfrrCheckpointPolicy=fixed",
                std::string("--compfrrInputPolicy=")+input}); });
        Reject([] { Parse({"--protectionScheme=compfrr", "--compfrrCheckpointPolicy=fixed", "--compfrrPlacementPolicy=compfrr"}); });
        for (const auto* ablation : {"noR", "noU", "noM"})
            Reject([&] { Parse({"--protectionScheme=compfrr", "--compfrrPlacementPolicy=compfrr",
                "--compfrrPressureModel=idle-aware", std::string("--compfrrPlacementAblation=")+ablation}); });
        Reject([] { Parse({"--protectionScheme=compfrr", "--compfrrPlacementPolicy=fa-ffp", "--compfrrPressureModel=cumulative"}); });
        Reject([] { Parse({"--protectionScheme=compfrr", "--compfrrFixedDelta=0.05"}); });
        Reject([] { Parse({"--protectionScheme=compfrr"}, false, "none"); });
        Reject([] { Parse({"--protectionScheme=compfrr"}, false, "generate", false, false); });
        Reject([] { Parse({"--protectionScheme=compfrr", "--compfrr-shadow=1"}); });
        Reject([] { Parse({"--compfrr-shadow=1"}, false, "none"); });
        Reject([] { Parse({"--compfrr-shadow-output=unused"}); });
        Reject([] { Parse({"--protectionScheme=multitree"}); });
        Reject([] { Parse({"--protectionScheme=off", "--protectionScheme=off"}); });
        Reject([] { Parse({"--testBaselinePlacement=lrl"}, true); });
        Reject([] { Parse({"--protectionScheme=recompute", "--testCbSatBusyPolicy=relocate"}, true); });
        Reject([] { Parse({"--protectionScheme=off", "--backupStorageBytesPerNode=10"}); });
        for (const auto* delta : {"0", "-0.1", "1.1", "0.0001", "nan", "inf"})
            Reject([&] { Parse({"--protectionScheme=compfrr", "--compfrrCheckpointPolicy=fixed",
                std::string("--compfrrFixedDelta=")+delta}); });
        for (const auto* batch : {"0", "21", "-1", "4294967296"})
            Reject([&] { Parse({"--protectionScheme=compfrr", "--compfrrCheckpointPolicy=fixed",
                std::string("--compfrrFixedBatchN=")+batch}); });
        std::cout << "protection config capability checks PASS: " << checks << '\n';
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
