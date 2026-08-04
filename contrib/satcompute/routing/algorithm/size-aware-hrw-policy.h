/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SIZE_AWARE_HRW_POLICY_H
#define SATCOMPUTE_SIZE_AWARE_HRW_POLICY_H

#include "next-hop-policy.h"
#include "../state/flow-route-state.h"
#include "../state/size-aware-load-view.h"

namespace ns3
{

class SizeAwareHrwPolicy : public NextHopPolicy
{
  public:
    SizeAwareHrwPolicy(FlowRouteState& flowState, const SizeAwareLoadView& loadView);

    NextHopDecision Select(const NextHopSelectionContext& context,
                           const std::vector<EcmpRouteCandidate>& candidates) override;

  private:
    FlowRouteState* m_flowState;
    const SizeAwareLoadView* m_loadView;
};

} // namespace ns3

#endif
