/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_TOPOLOGY_CONTROLLER_H
#define SATCOMPUTE_SATELLITE_TOPOLOGY_CONTROLLER_H

#include "satellite-runtime-view.h"

#include <cstdint>

namespace ns3
{

struct ResolvedSatComputeConfig;
class NodeContainer;
class SatelliteIdMap;
class SatelliteLinkState;

/** Common lifecycle and run counters for replay and online controllers. */
class SatelliteTopologyController : public SatelliteRuntimeView
{
  public:
    ~SatelliteTopologyController() override
    {
    }

    virtual void Initialize() = 0;
    virtual const ResolvedSatComputeConfig& GetConfig() const = 0;
    virtual const NodeContainer& GetNodes() const = 0;
    virtual const SatelliteIdMap& GetIdMap() const = 0;
    virtual const SatelliteLinkState& GetLinkState() const = 0;
    virtual uint32_t GetAppliedTopologySliceCount() const = 0;
    virtual uint32_t GetRouteComputationCount() const = 0;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_TOPOLOGY_CONTROLLER_H
