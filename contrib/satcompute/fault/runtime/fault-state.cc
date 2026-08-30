/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "fault-state.h"

#include <string>

namespace ns3
{

void
FaultState::Initialize(const std::vector<uint32_t>& satelliteIds)
{
    if (m_initialized || satelliteIds.empty())
    {
        throw FaultStateError("FaultState requires one non-empty initialization");
    }
    for (const uint32_t satelliteId : satelliteIds)
    {
        if (!m_nodes.emplace(satelliteId, FaultNodeAvailability{}).second)
        {
            throw FaultStateError("FaultState received duplicate satellite ID " +
                                  std::to_string(satelliteId));
        }
    }
    m_initialized = true;
}

void
FaultState::StartFault(const FaultDefinition& fault)
{
    auto node = m_nodes.find(fault.nodeId);
    if (!m_initialized || node == m_nodes.end())
    {
        throw FaultStateError("fault start references an unknown satellite ID");
    }
    if (m_activeFaultByNode.contains(fault.nodeId) ||
        !m_activeFaultIds.insert(fault.faultId).second)
    {
        throw FaultStateError("fault start overlaps active runtime state");
    }
    m_activeFaultByNode.emplace(fault.nodeId, fault.faultId);
    if (fault.faultType == FaultType::COMPUTE)
    {
        node->second.computeAvailable = false;
        return;
    }
    node->second.satelliteAvailable = false;
    node->second.communicationAvailable = false;
    node->second.computeAvailable = false;
}

void
FaultState::RecoverFault(const FaultDefinition& fault)
{
    auto node = m_nodes.find(fault.nodeId);
    const auto activeNode = m_activeFaultByNode.find(fault.nodeId);
    if (!m_initialized || node == m_nodes.end() ||
        activeNode == m_activeFaultByNode.end() || activeNode->second != fault.faultId ||
        !m_activeFaultIds.contains(fault.faultId))
    {
        throw FaultStateError("fault recovery does not match active runtime state");
    }
    m_activeFaultByNode.erase(activeNode);
    m_activeFaultIds.erase(fault.faultId);
    node->second = {};
}

bool
FaultState::HasNode(uint32_t nodeId) const
{
    return m_nodes.contains(nodeId);
}

const FaultNodeAvailability&
FaultState::GetNodeAvailability(uint32_t nodeId) const
{
    const auto node = m_nodes.find(nodeId);
    if (!m_initialized || node == m_nodes.end())
    {
        throw FaultStateError("FaultState has no requested satellite ID");
    }
    return node->second;
}

bool
FaultState::IsSatelliteAvailable(uint32_t nodeId) const
{
    return GetNodeAvailability(nodeId).satelliteAvailable;
}

bool
FaultState::IsCommunicationAvailable(uint32_t nodeId) const
{
    return GetNodeAvailability(nodeId).communicationAvailable;
}

bool
FaultState::IsComputeAvailable(uint32_t nodeId) const
{
    return GetNodeAvailability(nodeId).computeAvailable;
}

std::set<uint32_t>
FaultState::GetCommunicationUnavailableNodeIds() const
{
    std::set<uint32_t> unavailable;
    for (const auto& [nodeId, availability] : m_nodes)
    {
        if (!availability.communicationAvailable)
        {
            unavailable.insert(nodeId);
        }
    }
    return unavailable;
}

const std::set<uint64_t>&
FaultState::GetActiveFaultIds() const
{
    return m_activeFaultIds;
}

} // namespace ns3
