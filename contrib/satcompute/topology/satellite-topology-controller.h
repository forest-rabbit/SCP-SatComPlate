/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_TOPOLOGY_CONTROLLER_H
#define SATCOMPUTE_SATELLITE_TOPOLOGY_CONTROLLER_H

#include "satellite-runtime-view.h"

#include <cstdint>
#include <set>

namespace ns3
{

class NodeContainer;
class SatelliteIdMap;
class SatelliteLinkState;

/** 在线卫星拓扑的公共生命周期和运行计数接口。 */
class SatelliteTopologyController : public SatelliteRuntimeView
{
  public:
    ~SatelliteTopologyController() override
    {
    }

    virtual void Initialize() = 0;
    virtual const NodeContainer& GetNodes() const = 0;
    virtual const SatelliteIdMap& GetIdMap() const = 0;
    virtual const SatelliteLinkState& GetLinkState() const = 0;
    virtual bool ApplyCommunicationFaultOverlay(
        const std::set<uint32_t>& unavailableSatelliteIds,
        bool refreshNaturalState) = 0;
    virtual uint32_t GetAppliedTopologySliceCount() const = 0;
    virtual uint32_t GetRouteComputationCount() const = 0;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_TOPOLOGY_CONTROLLER_H
