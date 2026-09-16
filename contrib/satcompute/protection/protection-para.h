/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_PARA_H
#define SATCOMPUTE_PROTECTION_PARA_H
#include "common/input-contract.h"
#include "policy/recovery-policy.h"
#include "policy/compfrr/placement/compute-pressure/compute-pressure.h"
#include <cstdint>
#include <string>
#include <set>

namespace ns3::protection
{
enum class ProtectionScheme { OFF, COMPFRR, RECOMPUTE, ONE_PLUS_ONE, CB_SAT, MULTITREE };
enum class CheckpointPolicyKind { FIXED, ADAPTIVE };
enum class BaselinePlacementKind { FFP, FA_FFP, LRL, FA_LRL };
enum class PlacementPolicyKind { FFP, FA_FFP, LRL, FA_LRL, COMPFRR };
enum class PlacementAblation { NONE, NO_R, NO_U, NO_M };
using RecoveryFallbackKind = RemoteBusyRecoveryPolicy;

struct ProtectionCommonConfig
{
    uint64_t backupStorageBytesPerNode; ///< 额外 checkpoint 存储，Byte；不用于普通副本工作集。
};
struct CommonPlacementConfig
{
    uint32_t lrlRecoveryWeight; ///< LRL 活动恢复权重；正式配置固定 1。
};
struct FixedCheckpointConfig
{
    double delta; ///< 进度比例，0.05 表示 5%；内部边界转换为 permille。
    uint32_t batchN; ///< 每个 remote batch 的有效 L1 数量。
};
struct CompFrrConfig
{
    CheckpointPolicyKind checkpointPolicy;
    PlacementPolicyKind placementPolicy;
    InputPolicy inputPolicy;
    RecoveryFallbackKind recoveryPolicy;
    ComputePressurePolicy pressureModel;
    PlacementAblation placementAblation;
    FixedCheckpointConfig fixed;
};
struct RecomputeConfig { BaselinePlacementKind placementPolicy; };
struct OnePlusOneConfig { BaselinePlacementKind placementPolicy; };
struct MultiTreeConfig { BaselinePlacementKind placementPolicy; };
struct CbSatConfig
{
    BaselinePlacementKind placementPolicy;
    RecoveryFallbackKind busyPolicy; ///< CB 原忙时分支，不继承 CompFRR 的 deadline/tail 策略。
};
struct ProtectionDiagnosticsConfig
{
    bool compfrrShadow; ///< 独立只读观察器，只能在真实保护 off 时启用。
    std::string compfrrShadowOutput; ///< 空时使用 outputDir/shadow。
    std::set<uint64_t> residualDeadlineTaskIds; ///< 空集合关闭；只读 candidate 诊断，不参与策略。
};
/** 唯一配置 owner；不持有任何仿真资源或 controller。 */
struct ProtectionConfig
{
    ProtectionScheme scheme;
    ProtectionCommonConfig common;
    CommonPlacementConfig commonPlacement;
    CompFrrConfig compfrr;
    RecomputeConfig recompute;
    OnePlusOneConfig onePlusOne;
    CbSatConfig cbSat;
    MultiTreeConfig multitree;
    ProtectionDiagnosticsConfig diagnostics;
};
/** Return the single set of typed defaults, before CLI overrides and validation. */
ProtectionConfig GetDefaultProtectionConfig();
} // namespace ns3::protection
#endif
