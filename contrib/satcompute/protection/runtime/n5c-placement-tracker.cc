/* SPDX-License-Identifier: GPL-2.0-only */
#include "n5c-placement-tracker.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
int64_t Now() { return Simulator::Now().GetNanoSeconds(); }
uint64_t Add(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
        throw std::overflow_error("N5C physical peak overflow");
    return a + b;
}
} // namespace
N5cPlacementTracker::N5cPlacementTracker(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults,
    CheckpointManager& manager, int64_t stop, N5cVariant variant, bool spatialDiagnostics)
    : m_tasks(tasks), m_faults(faults), m_manager(manager), m_stopNs(stop), m_variant(variant),
      m_spatialDiagnostics(spatialDiagnostics)
{
    for (const auto& [node, pool] : manager.Pools())
    {
        m_nodes.emplace(node, N5cNodeObservation{});
        pool->SetChangeObserver([this, node] { ObserveStorage(node); });
        ObserveStorage(node);
    }
}
N5cPlacementTracker::~N5cPlacementTracker()
{
    for (const auto& [node, pool] : m_manager.Pools()) pool->SetChangeObserver({});
}
Ptr<ComputeService> N5cPlacementTracker::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node) return service;
    throw std::logic_error("N5C missing compute service");
}
void N5cPlacementTracker::FillResources(N5cCandidate& c) const
{
    const auto service = Service(c.remoteNode);
    const auto busy = service->GetBusyTimeNs();
    c.recoveryBusyNs = service->GetRecoveryBusyTimeNs();
    if (c.recoveryBusyNs > busy) throw std::logic_error("N5C recovery exceeds total actual service");
    c.normalBusyNs = busy - c.recoveryBusyNs;
    c.exposureNs = m_faults->ObservedSurvivalExposureNs(c.remoteNode);
    const auto& pool = *m_manager.Pools().at(c.remoteNode);
    c.capacityBytes = pool.Capacity();
    c.accountedBytes = m_quotas.Accounted(c.remoteNode, pool.OccupancyByTask());
}
uint64_t N5cPlacementTracker::FreeFor(uint32_t node, uint64_t replacing) const
{
    const auto& pool = *m_manager.Pools().at(node);
    const auto account = m_quotas.Accounted(node, pool.OccupancyByTask(), replacing);
    return account < pool.Capacity() ? pool.Capacity() - account : 0;
}
uint64_t N5cPlacementTracker::PeakFor(uint32_t node, uint64_t task, uint64_t additional) const
{
    const auto actual = m_manager.Pools().at(node)->OccupancyByTask();
    const auto found = actual.find(task);
    return Add(found == actual.end() ? 0 : found->second, additional);
}
bool N5cPlacementTracker::CanCommit(uint64_t task, uint32_t node, uint64_t peak) const
{
    const auto& pool = *m_manager.Pools().at(node);
    const auto actual = pool.OccupancyByTask();
    const auto own = actual.contains(task) ? actual.at(task) : 0;
    const auto others = m_quotas.Accounted(node, actual, task) - own;
    return others <= pool.Capacity() && std::max(own, peak) <= pool.Capacity() - others;
}
void N5cPlacementTracker::CommitQuota(uint64_t task, uint32_t node, uint64_t peak)
{
    if (!CanCommit(task, node, peak)) throw std::logic_error("N5C quota changed before commit");
    m_quotas.Replace(task, node, peak);
}
void N5cPlacementTracker::ReleaseQuota(uint64_t task)
{
    m_quotas.Release(task);
    m_ready.erase(task);
}
void N5cPlacementTracker::Assignment(uint64_t task, uint32_t node, bool active)
{
    if (!active && !m_assignments.contains(task)) return;
    if (active && !m_assignments.emplace(task, node).second)
        throw std::logic_error("N5C duplicate assignment");
    if (!active)
    {
        if (m_assignments.at(task) != node) throw std::logic_error("N5C release on wrong remote");
        m_assignments.erase(task);
    }
    auto& s = m_nodes.at(node);
    s.assignmentNs += static_cast<unsigned __int128>(Now() - s.assignmentAtNs) * s.active;
    s.assignmentAtNs = Now();
    if (active)
    {
        ++s.assignments; ++s.active;
        s.peakActive = std::max(s.peakActive, s.active);
    }
    else
    {
        if (!s.active) throw std::logic_error("N5C assignment count underflow");
        --s.active;
        ReleaseQuota(task);
    }
}
void N5cPlacementTracker::Initialized(uint64_t task) { m_ready[task] = Now(); }
std::optional<int64_t> N5cPlacementTracker::ReadyAfter(uint64_t task) const
{
    const auto found = m_ready.find(task);
    return found == m_ready.end() ? std::nullopt : std::optional{found->second};
}
void N5cPlacementTracker::ObserveStorage(uint32_t node)
{
    auto& s = m_nodes.at(node);
    s.storageByteNs += static_cast<unsigned __int128>(Now() - s.storageAtNs) * s.storageBytes;
    s.storageAtNs = Now();
    const auto& pool = *m_manager.Pools().at(node);
    s.storageBytes = pool.Used() + pool.Reserved();
    s.peakStorage = std::max(s.peakStorage, s.storageBytes);
}
size_t N5cPlacementTracker::Record(N5cDecisionTrace trace)
{
    trace.referenceInput.storageDemand = {};
    trace.referenceInput.risk.futureSteps = {};
    for (auto& c : trace.candidates)
    {
        c.demand.input.storageDemand = {};
        c.demand.input.risk.futureSteps = {};
        c.peers = {};
    }
    m_decisions.push_back(std::move(trace));
    return m_decisions.size() - 1;
}
void N5cPlacementTracker::Resolve(size_t index, bool committed, const std::string& reason)
{
    auto& trace = m_decisions.at(index);
    trace.committed = committed;
    trace.resolution = reason;
}
void N5cPlacementTracker::Finalize()
{
    if (m_finalized) return;
    for (auto& [node, s] : m_nodes)
    {
        ObserveStorage(node);
        s.assignmentNs += static_cast<unsigned __int128>(Now() - s.assignmentAtNs) * s.active;
        s.assignmentAtNs = Now();
        if (s.active || s.storageBytes) throw std::logic_error("N5C final ownership/storage leak");
    }
    if (!m_quotas.Empty() || !m_assignments.empty()) throw std::logic_error("N5C final quota/assignment leak");
    m_finalized = true;
}
} // namespace ns3::protection
