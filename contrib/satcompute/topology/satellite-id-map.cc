/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "satellite-id-map.h"

#include <algorithm>
#include <string>

namespace ns3
{

SatelliteIdMap::SatelliteIdMap(const NodeContainer& nodes,
                               const std::vector<uint32_t>& satelliteIdsByNodeIndex)
    : m_nodes(nodes),
      m_satelliteIdsByNodeIndex(satelliteIdsByNodeIndex),
      m_canonicalSatelliteIds(satelliteIdsByNodeIndex)
{
    if (nodes.GetN() == 0)
    {
        throw SatelliteIdMapError("satellite node set must not be empty");
    }
    if (nodes.GetN() != satelliteIdsByNodeIndex.size())
    {
        throw SatelliteIdMapError("satellite ID count must equal ns-3 node count");
    }
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        if (nodes.Get(index) == nullptr)
        {
            throw SatelliteIdMapError("satellite node container contains a null node");
        }
        const uint32_t satelliteId = satelliteIdsByNodeIndex.at(index);
        if (!m_nodeIndexes.emplace(satelliteId, index).second)
        {
            throw SatelliteIdMapError("duplicate external satellite ID " +
                                      std::to_string(satelliteId));
        }
    }
    std::sort(m_canonicalSatelliteIds.begin(), m_canonicalSatelliteIds.end());
}

uint32_t
SatelliteIdMap::GetNodeCount() const
{
    return m_nodes.GetN();
}

const NodeContainer&
SatelliteIdMap::GetNodes() const
{
    return m_nodes;
}

Ptr<Node>
SatelliteIdMap::GetNodeByIndex(uint32_t nodeIndex) const
{
    if (nodeIndex >= m_nodes.GetN())
    {
        throw SatelliteIdMapError("satellite node index is out of range: " +
                                  std::to_string(nodeIndex));
    }
    return m_nodes.Get(nodeIndex);
}

Ptr<Node>
SatelliteIdMap::GetNodeBySatelliteId(uint32_t satelliteId) const
{
    return GetNodeByIndex(GetNodeIndexBySatelliteId(satelliteId));
}

bool
SatelliteIdMap::HasSatelliteId(uint32_t satelliteId) const
{
    return m_nodeIndexes.contains(satelliteId);
}

uint32_t
SatelliteIdMap::GetNodeIndexBySatelliteId(uint32_t satelliteId) const
{
    const auto node = m_nodeIndexes.find(satelliteId);
    if (node == m_nodeIndexes.end())
    {
        throw SatelliteIdMapError("unknown external satellite ID " +
                                  std::to_string(satelliteId));
    }
    return node->second;
}

uint32_t
SatelliteIdMap::GetSatelliteIdByNodeIndex(uint32_t nodeIndex) const
{
    if (nodeIndex >= m_satelliteIdsByNodeIndex.size())
    {
        throw SatelliteIdMapError("satellite node index is out of range: " +
                                  std::to_string(nodeIndex));
    }
    return m_satelliteIdsByNodeIndex.at(nodeIndex);
}

const std::vector<uint32_t>&
SatelliteIdMap::GetSatelliteIdsByNodeIndex() const
{
    return m_satelliteIdsByNodeIndex;
}

const std::vector<uint32_t>&
SatelliteIdMap::GetCanonicalSatelliteIds() const
{
    return m_canonicalSatelliteIds;
}

} // namespace ns3
