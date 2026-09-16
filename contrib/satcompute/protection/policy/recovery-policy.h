/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RECOVERY_POLICY_H
#define SATCOMPUTE_RECOVERY_POLICY_H
#include <string_view>

namespace ns3::protection
{
/** Scheme-owned optional capabilities; busy policy does not grant a missing mechanism. */
struct CheckpointRecoveryCapabilities
{
    bool checkpoint{true};
    bool localTail{true};
    bool relocation{true};
};

/** Switch busy and direct-deadline fallback; other unavailability branches are shared. */
enum class RemoteBusyRecoveryPolicy
{
    RECOMPUTE,
    RELOCATE
};

/** Other unavailability reasons retain shared runtime recovery semantics. */
inline bool AllowsCheckpointRelocation(RemoteBusyRecoveryPolicy policy,
                                       std::string_view fallbackReason)
{
    return (fallbackReason != "REMOTE_BUSY" && fallbackReason != "DIRECT_DEADLINE_INFEASIBLE") ||
           policy == RemoteBusyRecoveryPolicy::RELOCATE;
}
} // namespace ns3::protection
#endif
