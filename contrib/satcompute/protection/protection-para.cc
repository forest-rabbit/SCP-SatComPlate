/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-para.h"

namespace ns3::protection
{
ProtectionConfig GetDefaultProtectionConfig()
{
    ProtectionConfig config{};

    // 正式实验默认启用 CompFRR；OFF 仅供显式诊断和历史复现。
    config.scheme = ProtectionScheme::COMPFRR;

    // 公共资源：十进制 10 GB；LRL 正式实验权重固定 1。
    config.common.backupStorageBytesPerNode = 10'000'000'000;
    config.commonPlacement.lrlRecoveryWeight = 1;

    // 正式组合：自适应频率 + CompFRR-P + Selective + Relocate。
    config.compfrr.checkpointPolicy = CheckpointPolicyKind::ADAPTIVE;
    config.compfrr.placementPolicy = PlacementPolicyKind::COMPFRR;
    config.compfrr.inputPolicy = InputPolicy::SELECTIVE;
    config.compfrr.recoveryPolicy = RecoveryFallbackKind::RELOCATE;
    config.compfrr.pressureModel = ComputePressurePolicy::CUMULATIVE;
    config.compfrr.placementAblation = PlacementAblation::NONE;

    // Fixed：每 5% 进度生成检查点，每 4 份有效 L1 形成 remote batch。
    config.compfrr.fixed.delta = 0.05;
    config.compfrr.fixed.batchN = 4;

    // 完整基线的私有 canonical default；历史变体通过专用测试入口显式注入。
    config.recompute.placementPolicy = BaselinePlacementKind::FA_FFP;
    config.onePlusOne.placementPolicy = BaselinePlacementKind::FA_FFP;
    // Multi-tree owns its canonical placement; no public published-rule tuning controls.
    config.multitree.placementPolicy = BaselinePlacementKind::FA_FFP;
    config.cbSat.placementPolicy = BaselinePlacementKind::FA_FFP;
    // 主 CB-Sat 忙时重算；旧省略 busy 的命令必须显式映射成历史 RELOCATE。
    config.cbSat.busyPolicy = RecoveryFallbackKind::RECOMPUTE;

    // 独立诊断：默认关闭，不建立真实保护。
    config.diagnostics.compfrrShadow = false;
    config.diagnostics.compfrrShadowOutput = "";
    config.diagnostics.residualDeadlineTaskIds = {};
    return config;
}
} // namespace ns3::protection
