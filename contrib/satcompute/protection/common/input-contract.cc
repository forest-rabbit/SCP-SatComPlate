/* SPDX-License-Identifier: GPL-2.0-only */
#include "input-contract.h"
#include "task-state-adapter.h"
#include <stdexcept>

namespace ns3::protection
{
InputPolicy ParseInputPolicy(const std::string& name)
{
    if (name == "eager") return InputPolicy::EAGER;
    if (name == "deferred") return InputPolicy::DEFERRED;
    if (name == "selective") return InputPolicy::SELECTIVE;
    throw std::invalid_argument("unknown INPUT policy");
}

InputStagingPolicy InputLayoutFor(InputPolicy policy)
{
    switch (policy)
    {
    case InputPolicy::EAGER: return InputStagingPolicy::EAGER;
    case InputPolicy::DEFERRED:
    case InputPolicy::SELECTIVE: return InputStagingPolicy::DEFERRED;
    }
    throw std::invalid_argument("invalid INPUT policy");
}

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
