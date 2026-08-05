/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_TEST_CONFIG_FACTORY_H
#define SATCOMPUTE_TEST_CONFIG_FACTORY_H

#include "ns3/constellation-definition.h"
#include "ns3/para.h"

#include <cstdint>
#include <string>

namespace ns3::satcompute::test
{

/** 构造不依赖输入文件的最小圆轨道星座，供 C++ 单元测试使用。 */
inline ConstellationDefinition
MakeTestConstellation(uint32_t numOrbits,
                      uint32_t satellitesPerOrbit,
                      const std::string& pattern = "walker-star",
                      bool phaseDiff = true)
{
    const double raanSpanDeg = pattern == "walker-star" ? 180.0 : 360.0;
    const double phasingFactor = phaseDiff && numOrbits > 1 ? 1.0 : 0.0;
    return {{},
            LeoOrbitalShell(780.0,
                            86.4,
                            numOrbits,
                            satellitesPerOrbit,
                            phasingFactor,
                            raanSpanDeg)};
}

/** 在线拓扑测试所需的平台参数、星座和精确时间。 */
struct OnlineTestConfiguration
{
    SatComputeConfig parameters;
    ConstellationDefinition constellation;
    int64_t durationNs{};
    int64_t updateIntervalNs{};
};

/** 构造不读取 scenario 或 resolved 配置的在线测试输入。 */
inline OnlineTestConfiguration
MakeOnlineTestConfig(uint32_t numOrbits,
                     uint32_t satellitesPerOrbit,
                     const std::string& delayMode,
                     int64_t durationNs,
                     int64_t updateIntervalNs,
                     long double maxDistanceM)
{
    SatComputeConfig config = GetDefaultSatComputeConfig();
    config.simulationDurationSeconds = static_cast<double>(durationNs) / 1000000000.0;
    config.delayMode = delayMode;
    config.fixedDelaySeconds = delayMode == "fixed" ? 0.008 : 0.0;
    config.networkUpdateIntervalSeconds =
        static_cast<double>(updateIntervalNs) / 1000000000.0;
    config.maxIslDistanceMeters = static_cast<double>(maxDistanceM);
    config.routingMode = "global-first";
    config.outputDirectory = "/tmp/satcompute-test";
    return {config,
            MakeTestConstellation(numOrbits, satellitesPerOrbit),
            durationNs,
            updateIntervalNs};
}

} // namespace ns3::satcompute::test

#endif // SATCOMPUTE_TEST_CONFIG_FACTORY_H
