/* SPDX-License-Identifier: GPL-2.0-only */
#include "placement-resource-tracker.h"
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
PlacementResourceTracker::PlacementResourceTracker(Ptr<TaskCoordinator> tasks, Ptr<FaultModelEngine> faults,
    CheckpointManager& manager, int64_t stop)
    : m_tasks(tasks), m_faults(faults), m_manager(manager), m_stopNs(stop)
{
    if (Now() != 0) throw std::logic_error("N5C history must observe from simulation start");
    for (auto service : m_tasks->GetComputeServices())
    {
        const auto node = service->GetNodeId();
        if (!m_services.emplace(node, service).second || service->GetBusyTimeNs())
            throw std::logic_error("N5C history requires unique unstarted services");
        m_computeHistory.Observe(node, 0, ComputeUsageHistory::Activity::IDLE);
        service->ConnectStateObserver(MakeCallback(&PlacementResourceTracker::ObserveCompute, this));
    }
    for (const auto& [node, pool] : manager.Pools())
    {
        m_nodes.emplace(node, PlacementNodeObservation{});
        pool->SetChangeObserver([this, node] { ObserveStorage(node); });
        ObserveStorage(node);
    }
}
PlacementResourceTracker::~PlacementResourceTracker()
{
    for (const auto& [node, service] : m_services)
        service->DisconnectStateObserver(MakeCallback(&PlacementResourceTracker::ObserveCompute, this));
    for (const auto& [node, pool] : m_manager.Pools()) pool->SetChangeObserver({});
}
Ptr<ComputeService> PlacementResourceTracker::Service(uint32_t node) const
{
    for (auto service : m_tasks->GetComputeServices())
        if (service->GetNodeId() == node) return service;
    throw std::logic_error("N5C missing compute service");
}
void PlacementResourceTracker::ObserveCompute(uint32_t node, bool busy)
{
    using Activity = ComputeUsageHistory::Activity;
    m_computeHistory.Observe(node, Now(), !busy ? Activity::IDLE :
        (m_services.at(node)->HasRecoveryReservation() ? Activity::RECOVERY : Activity::NORMAL));
}
void PlacementResourceTracker::VerifyHistory(uint32_t node) const
{
    const auto service = Service(node);
    const auto usage = m_computeHistory.Query(node, 0, Now(), Now(),
                                             m_faults->ObservedSurvivalExposureNs(node));
    if (usage.recoveryNs != service->GetRecoveryBusyTimeNs() ||
        usage.normalNs + usage.recoveryNs != service->GetBusyTimeNs())
        throw std::logic_error("N5C event history disagrees with actual service counters");
}
void PlacementResourceTracker::FillResources(PlacementResourceSnapshot& c, int64_t remainingTimeNs) const
{
    if (remainingTimeNs < 0) throw std::logic_error("N5C negative remaining compute time");
    const auto service = Service(c.remoteNode);
    const auto busy = service->GetBusyTimeNs();
    c.recoveryBusyNs = service->GetRecoveryBusyTimeNs();
    if (c.recoveryBusyNs > busy) throw std::logic_error("N5C recovery exceeds total actual service");
    c.normalBusyNs = busy - c.recoveryBusyNs;
    c.exposureNs = m_faults->ObservedSurvivalExposureNs(c.remoteNode);
    VerifyHistory(c.remoteNode);
    c.historyHorizonNs = remainingTimeNs;
    c.continuousIdleNs = m_computeHistory.IdleTimeNs(c.remoteNode, Now());
    c.historyWindowEndNs = Now();
    c.historyWindowBeginNs = std::max<int64_t>(0, Now() - remainingTimeNs);
    const auto recent = m_computeHistory.Query(c.remoteNode, c.historyWindowBeginNs,
                                               Now(), Now(), c.exposureNs);
    c.recentNormalBusyNs = recent.normalNs;
    c.recentRecoveryBusyNs = recent.recoveryNs;
    c.recentExposureNs = recent.exposureNs;
    const auto& pool = *m_manager.Pools().at(c.remoteNode);
    c.capacityBytes = pool.Capacity();
    c.accountedBytes = m_quotas.Accounted(c.remoteNode, pool.OccupancyByTask(), {},
                                         pool.OccupancyByKind(StorageKind::INPUT_STAGING));
}
uint64_t PlacementResourceTracker::FreeFor(uint32_t node, uint64_t replacing) const
{
    const auto& pool = *m_manager.Pools().at(node);
    const auto account = m_quotas.Accounted(node, pool.OccupancyByTask(), replacing,
                                           pool.OccupancyByKind(StorageKind::INPUT_STAGING));
    return account < pool.Capacity() ? pool.Capacity() - account : 0;
}
uint64_t PlacementResourceTracker::MaintenanceFree(uint32_t node, uint64_t task) const
{
    auto free = FreeFor(node, task);
    if (const auto peak = m_quotas.Peak(task, node))
    {
        const auto actual = m_manager.Pools().at(node)->OccupancyByTask();
        const auto input = m_manager.Pools().at(node)->OccupancyByKind(StorageKind::INPUT_STAGING);
        const auto own = (actual.contains(task) ? actual.at(task) : 0) -
                         (input.contains(task) ? input.at(task) : 0);
        free = std::min(free, *peak > own ? *peak - own : 0);
    }
    return free;
}
uint64_t PlacementResourceTracker::PeakFor(uint32_t node, uint64_t task, uint64_t additional) const
{
    const auto actual = m_manager.Pools().at(node)->OccupancyByTask();
    const auto found = actual.find(task);
    const auto input = m_manager.Pools().at(node)->OccupancyByKind(StorageKind::INPUT_STAGING);
    return Add((found == actual.end() ? 0 : found->second) -
               (input.contains(task) ? input.at(task) : 0), additional);
}
bool PlacementResourceTracker::CanCommit(uint64_t task, uint32_t node, uint64_t peak) const
{
    const auto& pool = *m_manager.Pools().at(node);
    const auto actual = pool.OccupancyByTask();
    const auto input = pool.OccupancyByKind(StorageKind::INPUT_STAGING);
    const auto own = (actual.contains(task) ? actual.at(task) : 0) -
                     (input.contains(task) ? input.at(task) : 0);
    const auto others = m_quotas.Accounted(node, actual, task, input) - own;
    return others <= pool.Capacity() && std::max(own, peak) <= pool.Capacity() - others;
}
void PlacementResourceTracker::CommitQuota(uint64_t task, uint32_t node, uint64_t peak)
{
    if (!CanCommit(task, node, peak)) throw std::logic_error("N5C quota changed before commit");
    m_quotas.Replace(task, node, peak);
}
void PlacementResourceTracker::ReleaseQuota(uint64_t task)
{
    m_quotas.Release(task);
    m_ready.erase(task);
}
void PlacementResourceTracker::Assignment(uint64_t task, uint32_t node, bool active)
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
void PlacementResourceTracker::Initialized(uint64_t task) { m_ready[task] = Now(); }
std::optional<int64_t> PlacementResourceTracker::ReadyAfter(uint64_t task) const
{
    const auto found = m_ready.find(task);
    return found == m_ready.end() ? std::nullopt : std::optional{found->second};
}
void PlacementResourceTracker::ObserveStorage(uint32_t node)
{
    auto& s = m_nodes.at(node);
    s.storageByteNs += static_cast<unsigned __int128>(Now() - s.storageAtNs) * s.storageBytes;
    s.storageAtNs = Now();
    const auto& pool = *m_manager.Pools().at(node);
    s.storageBytes = pool.Used() + pool.Reserved();
    s.peakStorage = std::max(s.peakStorage, s.storageBytes);
}
void PlacementResourceTracker::Finalize()
{
    if (m_finalized) return;
    for (auto& [node, s] : m_nodes)
    {
        VerifyHistory(node);
        ObserveStorage(node);
        s.assignmentNs += static_cast<unsigned __int128>(Now() - s.assignmentAtNs) * s.active;
        s.assignmentAtNs = Now();
        if (s.active || s.storageBytes) throw std::logic_error("N5C final ownership/storage leak");
    }
    if (!m_quotas.Empty() || !m_assignments.empty()) throw std::logic_error("N5C final quota/assignment leak");
    m_finalized = true;
}
} // namespace ns3::protection
