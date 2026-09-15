/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_INPUT_DEPENDENCY_H
#define SATCOMPUTE_INPUT_DEPENDENCY_H
#include "../../task/task-coordinator.h"
#include <functional>

namespace ns3::protection
{
/** Neutral recovery dependency, not a CompFRR selection decision. */
enum class InputDependencyMode { FETCH, READY, IN_FLIGHT };
struct InputDependency
{
    InputDependencyMode mode{InputDependencyMode::FETCH};
    std::optional<int64_t> remainingNs; ///< Causal estimate only, NEVER receiver completion.
    uint64_t flowId{}, objectId{};
    uint32_t target{};
    std::string refetchReason; ///< WRONG_TARGET_REFETCH / FAILED_PREFETCH_REFETCH or empty.
    int64_t readyNs{-1}; ///< Actual prior receiver/local completion for READY only.
    std::string diagnostic; ///< Non-refetch explanation, e.g. PREFETCH_NOT_ESTABLISHED.
};

/** Optional INPUT owner port. Candidate resolution is always read-only.
 * Shared Recovery depends only on this port, never on a scheme's selector.
 */
class InputDependencyResolver
{
  public:
    virtual ~InputDependencyResolver() = default;
    virtual InputDependency Resolve(const TaskDefinition& task, uint32_t target) const = 0;
    /** Only after final recovery acceptance; callback confirms actual receiver completion. */
    virtual void Accept(uint64_t task, const InputDependency& dependency,
                        std::function<void(bool, uint64_t)> completed) = 0;
    virtual void Fault(uint64_t task, const TaskFaultNodeChange& change) = 0;
    virtual void Freeze(uint64_t task) = 0;
    virtual void ComputeStarted(uint64_t task, uint32_t target) = 0;
    virtual void Release(uint64_t task) = 0;
};
} // namespace ns3::protection
#endif
