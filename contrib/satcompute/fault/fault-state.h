/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_STATE_H
#define SATCOMPUTE_FAULT_STATE_H

#include "fault-definition.h"

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace ns3
{

class FaultStateError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct FaultNodeAvailability
{
    bool satelliteAvailable{true};
    bool communicationAvailable{true};
    bool computeAvailable{true};
};

/** Runtime availability overlay, independent from natural orbit/topology state. */
class FaultState
{
  public:
    void Initialize(const std::vector<uint32_t>& satelliteIds);
    void StartFault(const FaultDefinition& fault);
    void RecoverFault(const FaultDefinition& fault);

    bool HasNode(uint32_t nodeId) const;
    const FaultNodeAvailability& GetNodeAvailability(uint32_t nodeId) const;
    bool IsSatelliteAvailable(uint32_t nodeId) const;
    bool IsCommunicationAvailable(uint32_t nodeId) const;
    bool IsComputeAvailable(uint32_t nodeId) const;
    const std::set<uint64_t>& GetActiveFaultIds() const;

  private:
    bool m_initialized{};
    std::map<uint32_t, FaultNodeAvailability> m_nodes;
    std::map<uint32_t, uint64_t> m_activeFaultByNode;
    std::set<uint64_t> m_activeFaultIds;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_STATE_H
