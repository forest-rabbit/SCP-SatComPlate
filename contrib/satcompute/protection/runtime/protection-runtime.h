/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_PROTECTION_RUNTIME_H
#define SATCOMPUTE_PROTECTION_RUNTIME_H
#include "../common/protection-types.h"

namespace ns3::protection
{
/** Policy chooses actions, never sends packets or modifies task state directly. */
class ProtectionPolicy
{
  public:
    virtual ~ProtectionPolicy() = default;
    /** First primary dispatch; future policies may leave protection OFF. */
    virtual ProtectionAction OnTaskComputeStart(const ProtectionContext& context) = 0;
    /** Future reconfiguration only; may not rewrite already captured records. */
    virtual ProtectionAction OnProtectionEpoch(const ProtectionContext& context) = 0;
    /** Primary interruption, before logical task terminalization. */
    virtual ProtectionAction OnComputeFault(const ProtectionContext& context) = 0;
    /** Stop maintenance at computation completion, not result delivery. */
    virtual void OnTaskComputeComplete(AttemptKey attempt) = 0;
    /** Release policy-owned context after the sole logical task terminal outcome. */
    virtual void OnTaskTerminal(uint64_t taskId) = 0;
};

/** Mechanism executor boundary; real network/task integration belongs to G2/G3. */
class ProtectionMechanism
{
  public:
    virtual ~ProtectionMechanism() = default;
    /** Declare supported actions; runtime does not assume all protection is checkpoint. */
    virtual bool Supports(ActionKind kind) const = 0;
    /** Execute a causal action using one owner attempt token. */
    virtual void Execute(const ProtectionContext& context, const ProtectionAction& action) = 0;
    /** Offer an established mechanism the fault first; true means recovery was accepted.
     * A false return has no side effects and permits the policy's recompute fallback.
     */
    virtual bool OnComputeFault(const ProtectionContext& context) = 0;
    /** Cancel future maintenance without altering completed history. */
    virtual void OnTaskComputeComplete(AttemptKey attempt) = 0;
    /** Idempotently clean all task-owned transfers, storage and callbacks. */
    virtual void OnTaskTerminal(uint64_t taskId) = 0;
};

/** Small dispatcher, not a plugin framework; unbound in the production G1 executable. */
class ProtectionRuntime
{
  public:
    /** Policy and mechanisms must outlive this dispatcher. */
    ProtectionRuntime(ProtectionPolicy& policy, std::vector<ProtectionMechanism*> mechanisms);
    /** Dispatch the policy's first-start action. */
    void OnTaskComputeStart(const ProtectionContext& context);
    /** Dispatch an optional reconfiguration action. */
    void OnProtectionEpoch(const ProtectionContext& context);
    /** Dispatch fault-time fallback or checkpoint recovery handling. */
    void OnComputeFault(const ProtectionContext& context);
    /** Notify maintenance completion. */
    void OnTaskComputeComplete(AttemptKey attempt);
    /** Notify terminal cleanup. */
    void OnTaskTerminal(uint64_t taskId);

  private:
    /** Require exactly one handler for a nonempty action. */
    void Dispatch(const ProtectionContext& context, const ProtectionAction& action);
    ProtectionPolicy& m_policy;                     ///< External policy lifetime.
    std::vector<ProtectionMechanism*> m_mechanisms; ///< Explicit finite executor set.
};
} // namespace ns3::protection
#endif
