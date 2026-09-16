/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPUTE_PRESSURE_H
#define SATCOMPUTE_COMPUTE_PRESSURE_H
#include <cstdint>

namespace ns3::protection
{
/** Only these two policies are production U policies. noU is an ablation, never a value here. */
enum class ComputePressurePolicy { CUMULATIVE, IDLE_AWARE };
/** Unchanged actual busy/exposure ratio, with caller's existing domain validation. */
double CumulativeComputePressure(uint64_t busyNs, uint64_t exposureNs);
/** No tunable coefficient: global * H/(H+I), preserving floating-point association. */
double IdleAwareComputePressure(double global, int64_t remainingNs, int64_t idleNs);

} // namespace ns3::protection
#endif
