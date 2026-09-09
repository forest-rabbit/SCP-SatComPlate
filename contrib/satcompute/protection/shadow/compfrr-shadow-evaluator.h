/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_SHADOW_EVALUATOR_H
#define SATCOMPUTE_COMPFRR_SHADOW_EVALUATOR_H
#include "compfrr-shadow-task-state.h"
#include "ns3/fault-model-engine.h"
#include "ns3/task-coordinator.h"
#include <filesystem>
#include <map>

namespace ns3::compfrr
{
/** Causal, no-RNG, no-packet G4 observer; owns only virtual events and records. */
class ShadowEvaluator
{
  public:
    /** Bind before Simulator::Run; B must be the actual 10 Gbps link rate. */
    ShadowEvaluator(Ptr<TaskCoordinator> tasks,
                    Ptr<FaultModelEngine> risk,
                    const ComputeProfile& profile,
                    uint64_t bandwidthBps,
                    const std::filesystem::path& output);
    ~ShadowEvaluator();
    ShadowEvaluator(const ShadowEvaluator&) = delete;
    ShadowEvaluator& operator=(const ShadowEvaluator&) = delete;
    /** Write all-task and sparse-fault records after the real simulation ends. */
    void Finalize();

  private:
    /** Handle only already-applied real transitions. */
    void OnTask(const TaskEventRecord& event);
    /** One causal decision at compute START then every elapsed second. */
    void Decide(uint64_t id);
    /** Virtual initialization, L1 trigger, and L1 completion. */
    void Initialized(uint64_t id);
    void TriggerLocal(uint64_t id, uint64_t work);
    void CompleteLocal(uint64_t id, uint64_t work, int64_t triggeredNs);
    /** Plan a future legal target; never replay missed checkpoints. */
    void PlanTarget(uint64_t id);
    /** Use current n on retained pending L1; serialize each task's batches. */
    void FormBatches(uint64_t id);
    /** Commit only fully completed virtual batches. */
    void CompleteRemote(uint64_t id, uint64_t work, uint64_t bytes, uint64_t batch);
    /** Stop on the actual compute outcome, cancel outstanding virtual events. */
    void Stop(uint64_t id, bool failed);
    /** Current real completed WU and causal decision inputs. */
    uint64_t CompletedWork(uint64_t id) const;
    DecisionInput Input(uint64_t id) const;
    /** Is the real task still computing and its compute node available? */
    bool Running(uint64_t id) const;
    /** Append one auditable virtual event. */
    void Event(uint64_t id, const std::string& type, nlohmann::json detail = {});

    Ptr<TaskCoordinator> m_tasks;                  ///< Read-only business observer source.
    Ptr<FaultModelEngine> m_risk;                  ///< Pure current-state query, not audit CSV.
    std::map<uint64_t, const TaskRuntime*> m_real; ///< Stable immutable views.
    std::map<uint64_t, ShadowTaskState> m_states;  ///< Virtual state by stable task ID.
    double m_bandwidth{};                          ///< Actual link bit/s / 8.
    std::filesystem::path m_output;                ///< Dedicated optional output directory.
    std::vector<nlohmann::json> m_decisions;       ///< Decision tick audit.
    std::vector<nlohmann::json> m_events;          ///< Virtual event audit.
    std::vector<nlohmann::json> m_faults;          ///< Already observed direct victims only.
    bool m_finalized{};                            ///< Prevent duplicate final output.
};
} // namespace ns3::compfrr
#endif
