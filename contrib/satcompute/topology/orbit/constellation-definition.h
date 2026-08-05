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

/** ISL 直线距离校验使用的最低离地高度，单位为米。 */
inline constexpr long double SATCOMPUTE_MINIMUM_ISL_RAY_ALTITUDE_METERS = 80000.0L;

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
 * 当前实现只支持一个 shell。注释和一行标题允许存在，唯一数据行使用
 * altitudeKm、inclinationDegrees、numberOfPlanes、
 * numberOfSatellitesPerPlane、phasingFactor、raanSpanDeg 六列。
 *
 * @param path 星座结构 CSV 路径。
 * @return 规范化输入路径和原生 shell。
 * @throws ConstellationDefinitionError 文件、数据行或取值无效时抛出。
 */
ConstellationDefinition LoadConstellationDefinition(const std::filesystem::path& path);

/**
 * 计算给定圆轨道高度下不低于指定离地高度的最长弦长。
 *
 * 地球半径与 ns-3.48 LeoCircularOrbitMobilityModel 使用的球形地球一致。
 *
 * @param altitudeKm 轨道离地高度，单位为千米。
 * @param minimumRayAltitudeMeters ISL 射线最低离地高度，单位为米。
 * @return 几何允许的最长弦长，单位为米，尚未向下取整。
 * @throws ConstellationDefinitionError 参数非有限、为负或轨道不高于射线下限时抛出。
 */
long double CalculateClearanceLimitedMaxIslDistanceMeters(
    long double altitudeKm,
    long double minimumRayAltitudeMeters);

/**
 * 校验 ISL 距离门限不超过当前 shell 的 80 km clearance 弦长上限。
 *
 * @param shell 已严格校验的原生 LEO shell。
 * @param configuredMaxIslDistanceMeters 配置的 ISL 最大距离，单位为米。
 * @throws ConstellationDefinitionError 距离或几何边界无效时抛出。
 */
void ValidateMaxIslDistanceAgainstOrbit(
    const LeoOrbitalShell& shell,
    long double configuredMaxIslDistanceMeters);

} // namespace ns3

#endif // SATCOMPUTE_CONSTELLATION_DEFINITION_H
