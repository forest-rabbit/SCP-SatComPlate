/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_TASK_STATE_ADAPTER_H
#define SATCOMPUTE_TASK_STATE_ADAPTER_H
#include "../../task/compute-task.h"
#include "protection-types.h"
#include <optional>
#include <vector>

namespace ns3::protection
{
/** Production task-state budget, independent of the G4 validation implementation. */
class TaskStateAdapter
{
  public:
    /** Validate frozen task mapping and construct legal application boundaries. */
    explicit TaskStateAdapter(const TaskDefinition& task);

    /** Total task WU. */
    uint64_t Work() const
    {
        return m_work;
    }

    /** Full variable state, including indices but excluding repeated record H. */
    uint64_t VariableBytes() const
    {
        return m_variable;
    }

    /** Per-record fixed metadata including decimal task label, excluding packet headers. */
    uint64_t HeaderBytes() const
    {
        return m_header;
    }

    /** Variable state at completed WU; LLM uses only whole tokens. */
    uint64_t StateBytes(uint64_t work) const;
    /** Frozen remaining-input plus state formula, or whole-token KV for LLM. */
    uint64_t CommittedStateBytes(uint64_t work) const;
    /** Explicit state-only deferred contract; the one-argument API stays eager-compatible. */
    uint64_t CommittedStateBytes(uint64_t work, InputStagingPolicy policy) const;
    /** Increment variable bytes plus one H; endpoints must be distinct legal boundaries. */
    uint64_t RecordBytes(uint64_t from, uint64_t to) const;
    /** Greatest legal boundary no later than completed work. */
    uint64_t Floor(uint64_t work) const;
    /** Next legal target after current/triggered work; delta is in per mille. */
    std::optional<uint64_t> Next(uint64_t current,
                                 uint64_t triggered,
                                 uint32_t deltaPermille) const;

    /** Exact legal boundary list for validation. */
    const std::vector<uint64_t>& Boundaries() const
    {
        return m_boundaries;
    }

  private:
    /** Reject progress beyond total task work. */
    void CheckWork(uint64_t work) const;
    uint64_t m_input{};                    ///< Serialized INPUT bytes.
    uint64_t m_work{};                     ///< Total WU.
    uint64_t m_variable{};                 ///< Full variable state.
    uint64_t m_header{};                   ///< Repeated record metadata.
    uint64_t m_extent{};                   ///< Application bytes or token count.
    bool m_llm{};                          ///< Whole-token state mapping.
    std::vector<uint64_t> m_boundaries{0}; ///< Canonical WU boundaries.
    std::vector<uint64_t> m_ends{0};       ///< Corresponding application extents.
};

/** Shared production cost source; integer ns avoid round-trip floating conversions. */
struct ProtectionCosts
{
    int64_t localNs;  ///< Asynchronous generation delay and equivalent cost.
    int64_t remoteNs; ///< Asynchronous merge delay and equivalent cost.
};

/** Costs classified by full variable-state bytes, not INPUT or current batch size. */
ProtectionCosts GetProtectionCosts(uint64_t fullVariableBytes);
} // namespace ns3::protection
#endif
