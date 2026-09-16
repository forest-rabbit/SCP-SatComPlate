/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_INPUT_COST_ADAPTER_H
#define SATCOMPUTE_INPUT_COST_ADAPTER_H
#include "../../../common/input-contract.h"

namespace ns3::protection
{
/** INPUT cost description parallel to the sole Frequency core; no optimizer or events. */
class InputCostAdapter
{
  public:
    explicit InputCostAdapter(InputStagingPolicy policy) : m_contract(policy) {}
    double InitializationSeconds(int64_t localNs, int64_t remoteNs,
                                 double baseSeconds, double stateSeconds) const;
    double FaultInputSeconds(bool replayAvailable, double bytes, double bandwidth) const;
    double StartInputLoss(double readyMass, double bytes, double bandwidth) const;
    bool RecoveryPathRequired() const { return m_contract.RequiresRecoveryInput(); }

  private:
    InputContract m_contract; ///< Descriptions only, never a second START/(delta,n) solve.
};
} // namespace ns3::protection
#endif
