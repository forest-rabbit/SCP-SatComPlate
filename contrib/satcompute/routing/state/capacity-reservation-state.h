/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CAPACITY_RESERVATION_STATE_H
#define SATCOMPUTE_CAPACITY_RESERVATION_STATE_H

#include "../algorithm/capacity-aware-path-types.h"
#include "../algorithm/capacity-aware-path-view.h"
#include "ns3/callback.h"

#include <cstdint>
#include <map>
#include <utility>

namespace ns3
{

struct CapacityAwareRuntimeSummary
{
    uint64_t activePathCountAtEnd;
    uint64_t reservedDirectedLinkCountAtEnd;
    uint64_t totalReservedRateBpsAtEnd;
    uint64_t pendingTransferCountAtEnd;

    CapacityAwareRuntimeSummary()
        : activePathCountAtEnd(0),
          reservedDirectedLinkCountAtEnd(0),
          totalReservedRateBpsAtEnd(0),
          pendingTransferCountAtEnd(0)
    {
    }
};

class CapacityReservationState
{
  public:
    uint64_t GetResidualRateBps(const CapacityAwarePathHop& hop) const;
    bool HasActivePath(uint64_t transferId) const;
    bool IsActivePathValid(uint64_t transferId,
                           uint32_t destinationSatelliteId,
                           const CapacityAwarePathView& pathView) const;
    CapacityAwareRuntimeSummary CollectSummary() const;
    void Reserve(uint64_t transferId, const CapacityAwarePath& path);
    void Release(uint64_t transferId);
    /** Observe post-change source/interface/rate without affecting admission.
     * @param observer Optional callback; an empty callback disables observations.
     */
    void SetObserver(Callback<void, uint32_t, uint32_t, uint64_t> observer);

  private:
    typedef std::pair<uint32_t, uint32_t> DirectedLinkKey;

    std::map<DirectedLinkKey, uint64_t> m_reservedRateBps;
    std::map<uint64_t, CapacityAwarePath> m_activePaths;
    Callback<void, uint32_t, uint32_t, uint64_t> m_observer; ///< Optional metrics observer.
};

} // namespace ns3

#endif
