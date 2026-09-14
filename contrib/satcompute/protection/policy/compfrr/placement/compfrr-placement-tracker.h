/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPFRR_PLACEMENT_TRACKER_H
#define SATCOMPUTE_COMPFRR_PLACEMENT_TRACKER_H
#include "compfrr-placement-policy.h"
#include "../../../runtime/placement-resource-tracker.h"
namespace ns3::protection
{
/** One START-only spatial proposal and its post-fault admission result. */
struct CompFrrDecisionTrace
{
    uint64_t taskId{};
    int64_t timeNs{};
    std::string trigger;
    PlacementDecision reference;
    PlacementForecastInput referenceInput;
    CheckpointCadence config;
    CompFrrSelection selection;
    std::vector<CompFrrCandidate> candidates; ///< Scalar snapshots only after recording.
    bool committed{};
    std::string resolution{"PENDING"};
};


/** P-owned proposal journal on top of the policy-neutral passive resource observer. */
class CompFrrPlacementTracker : public PlacementResourceTracker
{
  public:
    CompFrrPlacementTracker(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults,
                           CheckpointManager& manager, int64_t stopNs, N5cVariant variant,
                           bool spatialDiagnostics = true);
    size_t Record(CompFrrDecisionTrace trace);
    void Resolve(size_t index, bool committed, const std::string& reason);
    const CompFrrDecisionTrace& Decision(size_t index) const { return m_decisions.at(index); }
    const std::vector<CompFrrDecisionTrace>& Decisions() const { return m_decisions; }
    void Write(const std::filesystem::path& directory) const;
  private:
    N5cVariant m_variant; ///< Legacy external variant names are a compatibility contract.
    bool m_spatialDiagnostics;
    std::vector<CompFrrDecisionTrace> m_decisions;
};
} // namespace ns3::protection
#endif
