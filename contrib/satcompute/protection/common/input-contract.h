/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_INPUT_CONTRACT_H
#define SATCOMPUTE_INPUT_CONTRACT_H
#include <cstdint>

namespace ns3::protection
{
class TaskStateAdapter;

/** Public INPUT timing selector, independent of Frequency and its solver. */
enum class InputStagingPolicy
{
    EAGER,   ///< Stage full INPUT; preserve the legacy committed layout.
    DEFERRED ///< State-only protection; obtain original INPUT during recovery.
};

/** Pure initialization description. Execution/objects remain owned by the mechanism. */
struct InputInitialization
{
    uint64_t baseBytes{}; ///< Normal-period full INPUT, or zero logical identity.
    bool logicalBaseReady{}; ///< No BASE flow; still reserve/commit the logical object.
};

/** Full original INPUT dependency; local delivery is logical, never a fake network path. */
struct RecoveryInputRequirement
{
    bool required{}; ///< Recompute always needs original INPUT; checkpoint depends on mode.
    uint64_t bytes{}; ///< Logical bytes, including same-node delivery.
    bool local{}; ///< Source equals the actual recovery destination.
};

/** Neutral immutable contract consumed by checkpoint, recovery, storage and policies.
 * No events, reservations, routing, Frequency solver or future fault knowledge.
 */
class InputContract
{
  public:
    explicit constexpr InputContract(InputStagingPolicy policy) : m_policy(policy) {}
    constexpr bool RequiresRecoveryInput() const
    {
        return m_policy == InputStagingPolicy::DEFERRED;
    }
    InputInitialization DescribeInitialization(uint64_t inputBytes) const;
    uint64_t DescribeCommittedLayout(const TaskStateAdapter& layout, uint64_t work) const;
    RecoveryInputRequirement DescribeRecoveryInput(uint32_t source, uint32_t destination,
                                                   uint64_t inputBytes, bool recompute) const;

  private:
    InputStagingPolicy m_policy; ///< Existing public mode, no third timing variant.
};
} // namespace ns3::protection
#endif
