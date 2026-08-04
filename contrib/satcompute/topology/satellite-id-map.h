/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_ID_MAP_H
#define SATCOMPUTE_SATELLITE_ID_MAP_H

#include "ns3/node-container.h"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace ns3
{

class SatelliteIdMapError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/**
 * Stable bidirectional mapping between scenario satellite IDs and ns-3 nodes.
 *
 * The association is supplied explicitly and never inferred from Node::GetId().
 * Canonical satellite order is the ascending external-ID order, regardless of
 * node creation order.
 */
class SatelliteIdMap
{
  public:
    SatelliteIdMap(const NodeContainer& nodes,
                   const std::vector<uint32_t>& satelliteIdsByNodeIndex);

    uint32_t GetNodeCount() const;
    const NodeContainer& GetNodes() const;
    Ptr<Node> GetNodeByIndex(uint32_t nodeIndex) const;
    Ptr<Node> GetNodeBySatelliteId(uint32_t satelliteId) const;
    bool HasSatelliteId(uint32_t satelliteId) const;
    uint32_t GetNodeIndexBySatelliteId(uint32_t satelliteId) const;
    uint32_t GetSatelliteIdByNodeIndex(uint32_t nodeIndex) const;
    const std::vector<uint32_t>& GetSatelliteIdsByNodeIndex() const;
    const std::vector<uint32_t>& GetCanonicalSatelliteIds() const;

  private:
    NodeContainer m_nodes;
    std::vector<uint32_t> m_satelliteIdsByNodeIndex;
    std::vector<uint32_t> m_canonicalSatelliteIds;
    std::map<uint32_t, uint32_t> m_nodeIndexes;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_ID_MAP_H
