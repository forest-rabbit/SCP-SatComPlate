/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_RECOVERY_POLICY_H
#define SATCOMPUTE_RECOVERY_POLICY_H
#include <string_view>

namespace ns3::protection
{
/** Only the readable-checkpoint REMOTE_BUSY branch is an experimental choice. */
enum class RemoteBusyRecoveryPolicy
{
    RECOMPUTE,
    RELOCATE
};

/** Other unavailability reasons retain shared runtime recovery semantics. */
inline bool AllowsCheckpointRelocation(RemoteBusyRecoveryPolicy policy,
                                       std::string_view fallbackReason)
{
    return fallbackReason != "REMOTE_BUSY" || policy == RemoteBusyRecoveryPolicy::RELOCATE;
}
} // namespace ns3::protection
#endif
