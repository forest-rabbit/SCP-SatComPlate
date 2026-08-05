/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SATELLITE_LINK_H
#define SATCOMPUTE_SATELLITE_LINK_H

#include <cstdint>

namespace ns3
{

/** 一条按卫星 ID 规范化的无向候选链路。 */
struct SatelliteLink
{
    uint32_t sourceId{};
    uint32_t destinationId{};
    int64_t delayNs{};
    uint64_t bandwidthBps{};

    bool operator==(const SatelliteLink&) const = default;
};

} // namespace ns3

#endif // SATCOMPUTE_SATELLITE_LINK_H
