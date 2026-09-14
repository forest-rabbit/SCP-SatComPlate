/* SPDX-License-Identifier: GPL-2.0-only */
#include "input-contract.h"
#include "task-state-adapter.h"

namespace ns3::protection
{
InputInitialization InputContract::DescribeInitialization(uint64_t inputBytes) const
{
    return {RequiresRecoveryInput() ? 0 : inputBytes, RequiresRecoveryInput()};
}

uint64_t InputContract::DescribeCommittedLayout(const TaskStateAdapter& layout, uint64_t work) const
{
    // The one-argument adapter is the legacy eager layout, not this dispatch overload.
    return RequiresRecoveryInput() ? layout.StateBytes(work) : layout.CommittedStateBytes(work);
}

RecoveryInputRequirement InputContract::DescribeRecoveryInput(
    uint32_t source, uint32_t destination, uint64_t inputBytes, bool recompute) const
{
    const bool required = recompute || RequiresRecoveryInput();
    return {required, required ? inputBytes : 0, required && source == destination};
}
} // namespace ns3::protection
