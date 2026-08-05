/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TEST_CONFIG_FACTORY_H
#define SATCOMPUTE_TEST_CONFIG_FACTORY_H

#include "ns3/resolved-config.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace ns3::satcompute::test
{

/** 构造不依赖输入文件的最小圆轨道星座，供 C++ 单元测试使用。 */
inline ConstellationDefinition
MakeTestConstellation(uint32_t numOrbits,
                      uint32_t satellitesPerOrbit,
                      const std::string& pattern = "walker-star",
                      bool phaseDiff = true,
                      int64_t orbitEpochOffsetNs = 0)
{
    return {"0.1",
            {},
            "unit-test",
            pattern,
            numOrbits,
            satellitesPerOrbit,
            780000.0L,
            86.4L,
            phaseDiff,
            orbitEpochOffsetNs};
}

/** 构造完整的 online 内部配置，避免单元测试依赖已废弃的 scenario 输入。 */
inline ResolvedSatComputeConfig
MakeOnlineTestConfig(uint32_t numOrbits,
                     uint32_t satellitesPerOrbit,
                     const std::string& delayMode,
                     int64_t durationNs,
                     int64_t updateIntervalNs,
                     long double maxDistanceM)
{
    ResolvedSatComputeConfig config{};
    config.schemaVersion = "0.3";
    config.runName = "online-controller-test";
    config.simulation = {0, durationNs};
    config.constellation = MakeTestConstellation(numOrbits, satellitesPerOrbit);
    config.network = {"online",
                      std::nullopt,
                      "plus-grid",
                      false,
                      maxDistanceM,
                      delayMode,
                      delayMode == "fixed" ? std::optional<int64_t>(8000000)
                                           : std::nullopt,
                      updateIntervalNs,
                      2000000000,
                      1500,
                      1500000,
                      131072};
    config.routing = {"global-first", 1, "on-topology-change"};
    config.workloads = {std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        "fixed",
                        1024,
                        "strict"};
    config.traceExport = {false, 1000000000, true, "json-slices"};
    config.logging = {"summary", "summary", "off"};
    config.randomness = {1, 1, 0};
    config.outputDirectory = "/tmp/satcompute-test";
    return config;
}

/** 将 online 测试配置切换为 replay，其他行为参数保持不变。 */
inline ResolvedSatComputeConfig
MakeReplayTestConfig(ResolvedSatComputeConfig config,
                     const std::filesystem::path& replayDirectory)
{
    config.network.topologySource = "replay";
    config.network.replayDirectory = replayDirectory;
    config.traceExport.enabled = false;
    return config;
}

/** 构造具有明确星座规模、切片周期和链路参数的 replay 测试配置。 */
inline ResolvedSatComputeConfig
MakeReplayTestConfig(const std::filesystem::path& replayDirectory,
                     uint32_t numOrbits,
                     uint32_t satellitesPerOrbit,
                     int64_t durationNs,
                     int64_t updateIntervalNs,
                     const std::string& delayMode = "fixed",
                     std::optional<int64_t> fixedDelayNs = 1000000,
                     uint64_t linkBandwidthBps = 100000000)
{
    ResolvedSatComputeConfig config = MakeOnlineTestConfig(numOrbits,
                                                           satellitesPerOrbit,
                                                           delayMode,
                                                           durationNs,
                                                           updateIntervalNs,
                                                           30000000.0L);
    config.runName = "replay-test";
    config.network.delayMode = delayMode;
    config.network.fixedDelayNs = fixedDelayNs;
    config.network.linkBandwidthBps = linkBandwidthBps;
    return MakeReplayTestConfig(std::move(config), replayDirectory);
}

/** 构造路由、流量和任务测试共用的四节点动态菱形回放。 */
inline ResolvedSatComputeConfig
MakeDiamondReplayTestConfig(const std::filesystem::path& replayDirectory)
{
    return MakeReplayTestConfig(replayDirectory,
                                2,
                                2,
                                5000000000LL,
                                2000000000LL);
}

} // namespace ns3::satcompute::test

#endif // SATCOMPUTE_TEST_CONFIG_FACTORY_H
