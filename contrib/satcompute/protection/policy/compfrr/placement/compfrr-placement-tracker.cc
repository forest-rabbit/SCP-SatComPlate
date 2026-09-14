/* SPDX-License-Identifier: GPL-2.0-only */
#include "compfrr-placement-tracker.h"
namespace ns3::protection
{
CompFrrPlacementTracker::CompFrrPlacementTracker(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults,
    CheckpointManager& manager, int64_t stop, N5cVariant variant, bool spatialDiagnostics)
    : PlacementResourceTracker(tasks, faults, manager, stop), m_variant(variant),
      m_spatialDiagnostics(spatialDiagnostics)
{
}
size_t CompFrrPlacementTracker::Record(CompFrrDecisionTrace trace)
{
    trace.referenceInput.risk.futureSteps = {};
    for (auto& c : trace.candidates)
    {
        c.demand.input.risk.futureSteps = {};
        c.peers = {};
    }
    m_decisions.push_back(std::move(trace));
    return m_decisions.size() - 1;
}
void CompFrrPlacementTracker::Resolve(size_t index, bool committed, const std::string& reason)
{
    auto& trace = m_decisions.at(index);
    trace.committed = committed;
    trace.resolution = reason;
}
} // namespace ns3::protection
