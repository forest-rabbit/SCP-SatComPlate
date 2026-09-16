/* SPDX-License-Identifier: GPL-2.0-only */
#include "input-cost-adapter.h"
#include <algorithm>

namespace ns3::protection
{
double InputCostAdapter::InitializationSeconds(int64_t localNs, int64_t remoteNs,
                                               double baseSeconds, double stateSeconds) const
{
    const auto local = localNs / 1e9 + stateSeconds;
    return (m_contract.RequiresRecoveryInput() ? local : std::max(baseSeconds, local)) + remoteNs / 1e9;
}

double InputCostAdapter::FaultInputSeconds(bool replayAvailable, double bytes, double bandwidth) const
{
    return m_contract.RequiresRecoveryInput() && replayAvailable ? bytes / bandwidth : 0;
}

double InputCostAdapter::StartInputLoss(double readyMass, double bytes, double bandwidth) const
{
    // Preserve multiplication/division association, including the current double cast.
    return m_contract.RequiresRecoveryInput() ? 0 : readyMass * bytes / bandwidth;
}
} // namespace ns3::protection
