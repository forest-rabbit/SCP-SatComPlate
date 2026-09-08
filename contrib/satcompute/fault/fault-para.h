/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_PARA_H
#define SATCOMPUTE_FAULT_PARA_H

#include <cstdint>
#include <string>

namespace ns3
{

/** F1 指数温度状态与温度风险参数。 */
struct F1TemperatureParameters
{
    double baseC{}; ///< 空闲状态热平衡温度，单位为摄氏度。
    double saturationC{}; ///< 忙碌状态热平衡温度，单位为摄氏度。
    double riskC{}; ///< 温度风险曲线起点。
    double criticalC{}; ///< 确定性保护停机温度。
    double heatingTauSeconds{}; ///< 指数升温时间常数，单位为秒。
    double coolingTauSeconds{}; ///< 指数降温时间常数，单位为秒。
    double growthFactor{}; ///< 温度风险指数曲线形状参数。
};

/** F1 计算能源风险修正参数。 */
struct F1EnergyParameters
{
    bool enabled{}; ///< 能源压力是否参与 F1 风险。
    double initialDod{}; ///< 初始归一化放电深度。
    double riskDod{}; ///< 能源压力起始放电深度。
    double criticalDod{}; ///< 能源压力归一化上界。
    double batteryWh{}; ///< 电池能量容量，单位为 Wh。
    double incrementalComputePowerW{}; ///< 忙碌计算增量功率，单位为 W。
    double correctionWeight{}; ///< 能源压力进入综合风险的权重。
};

/** F1 自身状态计算故障参数。 */
struct F1FaultParameters
{
    bool enabled{}; ///< 是否启用 F1 在线判定。
    F1TemperatureParameters temperature; ///< 温度参数。
    F1EnergyParameters energy; ///< 能源风险修正参数。
    double riskThreshold{}; ///< 综合风险通知阈值。
    double maxFailureIntensityPerSecond{}; ///< F1 每秒最大故障强度。
};

/** F2 空间辐射风险与 SEU 映射参数。 */
struct F2FaultParameters
{
    bool enabled{}; ///< 是否启用 F2 在线判定。
    double longitudeMinDegrees{}; ///< 西侧经度边界，单位为度。
    double longitudeMaxDegrees{}; ///< 东侧经度边界，单位为度。
    double latitudeMinDegrees{}; ///< 南侧纬度边界，单位为度。
    double latitudeMaxDegrees{}; ///< 北侧纬度边界，单位为度。
    double hotspotLongitudeDegrees{}; ///< 经验 SEU 热点中心经度，单位为度。
    double hotspotLatitudeDegrees{}; ///< 经验 SEU 热点中心纬度，单位为度。
    double sigmaLongitudeWestDegrees{}; ///< 热点西侧高斯经度标准差，单位为度。
    double sigmaLongitudeEastDegrees{}; ///< 热点东侧高斯经度标准差，单位为度。
    double sigmaLatitudeDegrees{}; ///< 高斯空间风险的纬度标准差，单位为度。
    double spatialRiskThreshold{}; ///< 触发风险通知的当前空间风险阈值。
    double referenceSeuIntensityPerSecond{}; ///< 热点中心的参考 SEU 强度。
    double seuToComputeFailureProbability{}; ///< SEU 映射为计算故障的条件概率。
};

/** F3 永久整星故障参数。 */
struct F3FaultParameters
{
    bool enabled{}; ///< 是否启用 F3 在线判定。
    std::string mode; ///< fixed_k、poisson 或 controlled 单事件场景。
    uint32_t fixedCount{}; ///< fixed_k 模式下的永久故障卫星数量。
    double singleSatelliteIntensityPerSecond{}; ///< 单颗存活卫星的每秒泊松强度。
    uint32_t controlledNodeId{}; ///< controlled 场景目标，仅调度器读取。
    double controlledStartSeconds{}; ///< controlled 场景 START 秒数。
};

/** N4B 全部内置故障参数。 */
struct FaultParameters
{
    double checkIntervalSeconds{}; ///< F1/F2 更新与采样周期，单位为秒。
    double recoverableComputeDurationSeconds{}; ///< F1/F2 算力停机时间，单位为秒。
    F1FaultParameters f1; ///< F1 自身状态参数。
    F2FaultParameters f2; ///< F2 辐射暴露参数。
    F3FaultParameters f3; ///< F3 碎片撞击参数。
};

/**
 * 返回 SatCompute 唯一的一组内置故障参数。
 *
 * @return 按 common、F1、F2 和 F3 分组的参数。
 */
FaultParameters GetDefaultFaultParameters();

} // namespace ns3

#endif // SATCOMPUTE_FAULT_PARA_H
