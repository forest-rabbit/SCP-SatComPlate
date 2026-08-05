/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CONSTELLATION_DEFINITION_H
#define SATCOMPUTE_CONSTELLATION_DEFINITION_H

#include "ns3/leo-orbital-shell.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace ns3
{

/** 原生 LEO shell CSV 无法读取或不满足平台约束时抛出的异常。 */
class ConstellationDefinitionError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** 一个原生 ns-3.48 LEO shell 及其输入路径。 */
struct ConstellationDefinition
{
    std::filesystem::path sourcePath; ///< 规范化后的输入文件路径。
    LeoOrbitalShell shell;            ///< ns-3.48 原生圆轨道 shell。

    /**
     * 返回稳定卫星 ID 空间的大小。
     *
     * @return shell 轨道面数与每面卫星数的乘积。
     */
    uint32_t GetSatelliteCount() const;
};

/**
 * 读取并严格校验一个 ns-3.48 LeoOrbitalShell CSV。
 *
 * SatCompute v0.4 当前只支持一个 shell。注释和一行标题允许存在，唯一数据行
 * 使用 altitudeKm、inclinationDegrees、numberOfPlanes、
 * numberOfSatellitesPerPlane、phasingFactor、raanSpanDeg 六列。
 *
 * @param path 星座结构 CSV 路径。
 * @return 规范化输入路径和原生 shell。
 * @throws ConstellationDefinitionError 文件、数据行或取值无效时抛出。
 */
ConstellationDefinition LoadConstellationDefinition(const std::filesystem::path& path);

} // namespace ns3

#endif // SATCOMPUTE_CONSTELLATION_DEFINITION_H
