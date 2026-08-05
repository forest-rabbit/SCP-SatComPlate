/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CONSTELLATION_DEFINITION_H
#define SATCOMPUTE_CONSTELLATION_DEFINITION_H

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ns3
{

/** 星座结构 JSON 无法读取或不满足 closed-world 合同时抛出的异常。 */
class ConstellationDefinitionError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** 只描述轨道物理结构的星座定义。 */
struct ConstellationDefinition
{
    std::string schemaVersion;              ///< 星座结构合同版本。
    std::filesystem::path sourcePath;       ///< 规范化后的输入文件路径。
    std::string constellationName;          ///< 稳定星座名称。
    std::string constellationPattern;       ///< walker-star 或 walker-delta。
    uint32_t numOrbits;                     ///< 轨道面数量。
    uint32_t satellitesPerOrbit;            ///< 每个轨道面的卫星数量。
    long double altitudeM;                  ///< 圆轨道高度，单位为米。
    long double inclinationDeg;             ///< 轨道倾角，单位为度。
    bool phaseDiff;                         ///< 奇数轨道面是否偏移半个槽位。
    int64_t orbitEpochOffsetNs;             ///< t=0 时的轨道传播偏移，单位为纳秒。

    /**
     * 返回稳定卫星 ID 空间的大小。
     *
     * @return 轨道面数与每面卫星数的乘积。
     */
    uint32_t GetSatelliteCount() const;
};

/**
 * 读取并严格校验一个 constellation 0.1 JSON。
 *
 * @param path 星座结构 JSON 路径。
 * @return 规范化且已转换为整数纳秒的星座定义。
 * @throws ConstellationDefinitionError 文件、JSON、字段或取值无效时抛出。
 */
ConstellationDefinition LoadConstellationDefinition(const std::filesystem::path& path);

} // namespace ns3

#endif // SATCOMPUTE_CONSTELLATION_DEFINITION_H
