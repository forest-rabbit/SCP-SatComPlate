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

    // F1/F2 可恢复计算故障的保护停机时间，单位为秒。
    parameters.recoverableComputeDurationSeconds = 8.0;

    // F1: self-state compute fault

    // 是否启用 F1 在线风险与故障判定。
    parameters.f1.enabled = true;

    // 空闲时趋近的基础温度，单位为摄氏度。
    parameters.f1.temperature.baseC = 17.0;

    // 持续计算时趋近的热平衡温度，单位为摄氏度。
    parameters.f1.temperature.saturationC = 35.0;

    // 温度风险曲线的起点，单位为摄氏度。
    parameters.f1.temperature.riskC = 20.0;

    // 触发确定性保护停机的温度，单位为摄氏度。
    parameters.f1.temperature.criticalC = 30.0;

    // 指数升温时间常数，单位为秒，不是升温完成时间。
    parameters.f1.temperature.heatingTauSeconds = 43.0;

    // 指数降温时间常数，单位为秒，不是恢复停机时间。
    parameters.f1.temperature.coolingTauSeconds = 40.0;

    // riskC 到 criticalC 区间内的指数风险曲线形状。
    parameters.f1.temperature.growthFactor = 3.0;

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

    // 开启风险 episode 的综合风险阈值。
    parameters.f1.riskThreshold = 0.60;

    // F1 每秒最大故障强度。
    parameters.f1.maxFailureIntensityPerSecond = 0.005;

    // F2: radiation exposure compute fault

    // 默认运行仍保持 F2 关闭；F2-only 功能场景由平台运行参数显式启用。
    parameters.f2.enabled = false;

    // 辐射区域西侧经度边界，单位为度。
    parameters.f2.longitudeMinDegrees = -90.0;

    // 辐射区域东侧经度边界，单位为度。
    parameters.f2.longitudeMaxDegrees = 5.0;

    // 辐射区域南侧纬度边界，单位为度。
    parameters.f2.latitudeMinDegrees = -50.0;

    // 辐射区域北侧纬度边界，单位为度。
    parameters.f2.latitudeMaxDegrees = 5.0;

    // 66 星、1000 秒功能窗口标定的区域内每秒有效故障强度。
    parameters.f2.effectiveFailureIntensityPerSecond = 0.00015569048731122528;

    // 典型完整穿越进行到一半（461 秒）时的累计风险通知阈值。
    parameters.f2.riskThreshold = 0.06925814255738115;

    // 离开辐射区域后关闭 episode 并清零连续暴露。
    parameters.f2.resetExposureOnExit = true;

    // F3: fatal debris impact

    // N4B 第三阶段接入前保持关闭。
    parameters.f3.enabled = false;

    // fixed_k 为人工指定数量，poisson 为按强度抽样。
    parameters.f3.mode = "fixed_k";

    // fixed_k 下，一次仿真中人工设置的永久撞击卫星数量。
    parameters.f3.fixedCount = 1;

    // poisson 下每颗存活卫星的每秒撞击强度。
    parameters.f3.singleSatelliteIntensityPerSecond = 0.0;

    return parameters;
}

} // namespace ns3
