/* SPDX-License-Identifier: GPL-2.0-only */
#include "protection-transfer-dispatcher.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <stdexcept>

namespace ns3::protection
{
namespace
{
uint64_t MaximumId(Ptr<TaskCoordinator> tasks)
{
    if (!tasks)
        throw std::invalid_argument("checkpoint requires tasks");
    uint64_t maximum = 0;
    for (const auto& plan : tasks->GetTransferEngine()->GetPlans())
        maximum = std::max(maximum, plan.transferId);
    return maximum;
}
} // namespace

ProtectionTransferDispatcher::ProtectionTransferDispatcher(
    Ptr<TaskCoordinator> tasks, std::function<void(const Request&)> primary)
    : m_network(tasks ? tasks->GetTransferEngine() : nullptr), m_ids(MaximumId(tasks)),
      m_primary(std::move(primary))
{
}

ProtectionTransferDispatcher::~ProtectionTransferDispatcher()
{
    for (auto& [time, event] : m_flushEvents)
        Simulator::Cancel(event);
}

void ProtectionTransferDispatcher::Queue(int64_t time, Request request, const char* message)
{
    const auto key = request.key;
    if (!m_requests[time].emplace(key, std::move(request)).second)
        throw std::logic_error(message);
    if (!m_flushEvents.contains(time))
        m_flushEvents.emplace(time, Simulator::Schedule(NanoSeconds(1),
            &ProtectionTransferDispatcher::Flush, this, time));
}

void ProtectionTransferDispatcher::Flush(int64_t time)
{
    auto requests = m_requests.extract(time);
    m_flushEvents.erase(time);
    if (requests.empty()) return;
    for (const auto& [key, request] : requests.mapped())
        if (request.live) RegisterGuarded(request, time);
        else m_primary(request);
}

void ProtectionTransferDispatcher::RegisterGuarded(const Request& request, int64_t time)
{
    if (!request.live()) return;
    const auto id = m_ids.Next();
    NetworkTransfer plan;
    plan.transferId = id;
    plan.sourceSatelliteId = request.source;
    plan.destinationSatelliteId = request.destination;
    plan.sizeBytes = request.bytes;
    try
    {
        m_network->RegisterRuntimePlan(plan,
            request.key.kind == ProtectionTransferKind::RECOVERY_RESULT);
    }
    catch (const NetworkTransferConfigError&)
    {
        request.registered(0);
        return;
    }
    if (request.key.kind != ProtectionTransferKind::RECOVERY_RESULT)
        Record({request.key, id, request.bytes, request.work, request.object,
                request.destination, time});
    request.registered(id);
    m_network->StartTransferNow(id);
}

size_t ProtectionTransferDispatcher::Record(ProtectionFlow flow)
{
    const auto index = m_flows.size();
    m_flows.push_back(std::move(flow));
    return index;
}

void ProtectionTransferDispatcher::Discard(uint64_t taskId, bool primaryOnly)
{
    for (auto& [time, requests] : m_requests)
        std::erase_if(requests, [&](const auto& item) {
            return item.first.taskId == taskId && (!primaryOnly ||
                (item.first.attemptGeneration == 0 && item.first.kind != ProtectionTransferKind::PREFETCH_INPUT));
        });
}

void ProtectionTransferDispatcher::Finalize()
{
    for (auto& [time, event] : m_flushEvents) Simulator::Cancel(event);
    m_flushEvents.clear();
    m_requests.clear();
}

bool ProtectionTransferDispatcher::Empty() const
{
    for (const auto& [time, requests] : m_requests)
        if (!requests.empty()) return false;
    return true;
}
} // namespace ns3::protection
