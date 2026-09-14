/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_N5C_PLACEMENT_POLICY_H
#define SATCOMPUTE_N5C_PLACEMENT_POLICY_H
#include "compfrr-placement-policy.h"
#include "../../../storage/peak-quota-ledger.h"

namespace ns3::protection
{
/** Legacy exported types/functions, retained for fixtures and historical analysis clients. */
using N5cForecast = CompFrrForecast;
using N5cOccupancyWindow = CompFrrOccupancyWindow;
using N5cCandidate = CompFrrCandidate;
using N5cScore = CompFrrScore;
using N5cSelection = CompFrrSelection;
using N5cPlacementPolicy = CompFrrPlacementPolicy;
using N5cQuotaLedger = PeakQuotaLedger;
inline double N5cCatchSeconds(const N5cForecast& f) { return CompFrrCatchSeconds(f); }
inline double N5cBudgetSeconds(const N5cForecast& f, int64_t t) { return CompFrrBudgetSeconds(f, t); }
inline auto N5cRecoveryWindows(const N5cForecast& f) { return CompFrrRecoveryWindows(f); }
inline auto ScoreN5cCandidate(const N5cCandidate& c, N5cVariant v) { return ScoreCompFrrCandidate(c, v); }
inline double N5cRationalPressure(double g, int64_t h, int64_t i) { return IdleAwareComputePressure(g, h, i); }

} // namespace ns3::protection
#endif
