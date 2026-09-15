/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-para.h"

namespace ns3::protection
{
ProtectionConfig GetDefaultProtectionConfig()
{
    ProtectionConfig config{};

    // 总开关：默认不启用真实保护。
    config.scheme = ProtectionScheme::OFF;

    // 公共资源：十进制 10 GB；LRL 正式实验权重固定 1。
    config.common.backupStorageBytesPerNode = 10'000'000'000;
    config.commonPlacement.lrlRecoveryWeight = 1;

    // CompFRR：保持当前默认身份，不自动启用 CompFRR-P 或 Selective。
    config.compfrr.checkpointPolicy = CheckpointPolicyKind::ADAPTIVE;
    config.compfrr.placementPolicy = PlacementPolicyKind::FA_FFP;
    config.compfrr.inputPolicy = InputPolicy::EAGER;
    config.compfrr.recoveryPolicy = RecoveryFallbackKind::RELOCATE;
    config.compfrr.pressureModel = ComputePressurePolicy::CUMULATIVE;
    config.compfrr.placementAblation = PlacementAblation::NONE;

    // Fixed：每 5% 进度生成检查点，每 4 份有效 L1 形成 remote batch。
    config.compfrr.fixed.delta = 0.05;
    config.compfrr.fixed.batchN = 4;

    // 完整基线的私有 canonical default；历史变体通过专用测试入口显式注入。
    config.recompute.placementPolicy = BaselinePlacementKind::FA_FFP;
    config.onePlusOne.placementPolicy = BaselinePlacementKind::FA_FFP;
    config.cbSat.placementPolicy = BaselinePlacementKind::FA_FFP;
    // 主 CB-Sat 忙时重算；旧省略 busy 的命令必须显式映射成历史 RELOCATE。
    config.cbSat.busyPolicy = RecoveryFallbackKind::RECOMPUTE;

    // 独立诊断：默认关闭，不建立真实保护。
    config.diagnostics.compfrrShadow = false;
    config.diagnostics.compfrrShadowOutput = "";
    return config;
}
} // namespace ns3::protection
