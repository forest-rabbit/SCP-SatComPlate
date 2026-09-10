/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 集中定义 SatCompute 的故障模型参数；修改后需要重新编译。

#include "fault-para.h"

namespace ns3
{

FaultParameters
GetDefaultFaultParameters()
{
    FaultParameters parameters;

    // common

    // F1/F2 状态更新和实际计算故障采样周期，单位为秒。
    parameters.checkIntervalSeconds = 1.0;

    // F1: self-state compute fault

    // 是否启用 F1 在线风险与故障判定。
    parameters.f1.enabled = true;

    // 空闲时线性冷却到的基础温度，单位为摄氏度。
    parameters.f1.temperature.baseC = 17.0;

    // 持续计算时趋近的热平衡温度，单位为摄氏度。
    parameters.f1.temperature.saturationC = 35.0;

    // 温度风险曲线的起点，单位为摄氏度。
    parameters.f1.temperature.riskC = 20.0;

    // 触发确定性保护停机的温度，单位为摄氏度。
    parameters.f1.temperature.criticalC = 30.0;

    // 从 17 C 持续计算到 30 C 的秒数；升温系数由温度锚点和 gamma 派生。
    parameters.f1.temperature.heatingToCriticalSeconds = 30.0;

    // 升温形状 gamma；1 为旧指数。G3 v3 四组 pilot 后按热状态分布冻结为 1.5。
    parameters.f1.temperature.heatingShapeGamma = 1.5;

    // 从 30 C 线性冷却到 17 C 的秒数；实际 F1 恢复时长按故障温度派生。
    parameters.f1.temperature.coolingFromCriticalToBaseSeconds = 4.0;

    // riskC 到 criticalC 区间内的参考 1 秒概率曲线 beta；越小则中温段概率越高。
    parameters.f1.temperature.growthFactor = 10.0;

    // 是否把能源压力作为 F1 风险的小权重修正。
    parameters.f1.energy.enabled = true;

    // 初始归一化放电深度。
    parameters.f1.energy.initialDod = 0.25;

    // 能源压力开始增长的放电深度。
    parameters.f1.energy.riskDod = 0.30;

    // 能源压力归一化到 1 的放电深度。
    parameters.f1.energy.criticalDod = 0.50;

    // 电池能量容量，单位为 Wh。
    parameters.f1.energy.batteryWh = 230.0;

    // 忙碌计算相对空闲状态的增量功率，单位为 W。
    parameters.f1.energy.incrementalComputePowerW = 2.44;

    // 能源压力进入 F1 综合风险的权重。
    parameters.f1.energy.correctionWeight = 0.10;

    // F2: spatial radiation-induced compute fault

    // 当前冻结场景启用 F2；单源实验通过 CLI 显式关闭其他来源。
    parameters.f2.enabled = true;

    // F2 固定算力停机时间，与 F1 散热参数无关，单位为秒。
    parameters.f2.recoveryDurationSeconds = 8.0;

    // 辐射区域西侧经度边界，单位为度。
    parameters.f2.longitudeMinDegrees = -90.0;

    // 辐射区域东侧经度边界，单位为度。
    parameters.f2.longitudeMaxDegrees = 5.0;

    // 辐射区域南侧纬度边界，单位为度。
    parameters.f2.latitudeMinDegrees = -50.0;

    // 辐射区域北侧纬度边界，单位为度。
    parameters.f2.latitudeMaxDegrees = 5.0;

    // 文献观测到的 SAA 内 SEU 热点中心经度，单位为度。
    parameters.f2.hotspotLongitudeDegrees = -60.0;

    // 文献观测到的 SAA 内 SEU 热点中心纬度，单位为度。
    parameters.f2.hotspotLatitudeDegrees = -28.0;

    // 热点西侧的高斯空间风险场经度标准差，单位为度。
    parameters.f2.sigmaLongitudeWestDegrees = 12.0;

    // 热点东侧的高斯空间风险场经度标准差，单位为度。
    parameters.f2.sigmaLongitudeEastDegrees = 24.0;

    // 66 星轨道候选扫描冻结的高斯空间风险场纬度标准差，单位为度。
    parameters.f2.sigmaLatitudeDegrees = 12.0;

    // 独立 F2 暴露工具的高风险分类阈值；平台不产生风险通知。
    parameters.f2.spatialRiskThreshold = 0.50;

    // 66 星选定 1000 秒窗口目标均值为 2 时反标定的热点参考 SEU 强度。
    parameters.f2.referenceSeuIntensityPerSecond = 0.002859196111093899;

    // 冻结的场景级 SEU 到计算服务故障映射系数，不解释为实测概率。
    parameters.f2.seuToComputeFailureProbability = 0.50;

    // F3: fatal debris impact

    // 当前冻结场景启用一次受控永久整星故障。
    parameters.f3.enabled = true;

    // controlled 为固定节点/时刻；fixed_k 为指定数量，poisson 为按强度抽样。
    parameters.f3.mode = "controlled";

    // fixed_k 下，一次仿真中人工设置的永久撞击卫星数量。
    parameters.f3.fixedCount = 1;

    // poisson 下每颗存活卫星的每秒撞击强度。
    parameters.f3.singleSatelliteIntensityPerSecond = 0.0;

    // controlled 模式只安排这一颗卫星和一个精确时刻；不是文件 replay。
    parameters.f3.controlledNodeId = 62;
    parameters.f3.controlledStartSeconds = 1027.055770726;

    return parameters;
}

} // namespace ns3
