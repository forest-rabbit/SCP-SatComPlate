/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_N5C_PLACEMENT_TRACKER_H
#define SATCOMPUTE_N5C_PLACEMENT_TRACKER_H
#include "../policy/compfrr/placement/compfrr-placement-tracker.h"
#include "../policy/compfrr/placement/n5c-placement-policy.h"
namespace ns3::protection
{
using N5cPlacementTracker = CompFrrPlacementTracker;
using N5cDecisionTrace = CompFrrDecisionTrace;
using N5cNodeObservation = PlacementNodeObservation;
}
#endif
