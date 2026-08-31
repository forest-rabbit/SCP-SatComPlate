/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_F3_DEBRIS_FAULT_MODEL_H
#define SATCOMPUTE_F3_DEBRIS_FAULT_MODEL_H

#include "ns3/fault-para.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ns3
{

/** One precomputed permanent F3 satellite-fault event. */
struct F3DebrisFaultEvent
{
    int64_t startTimeNs{}; ///< Absolute event time in [0, simulation duration).
    uint32_t nodeId{}; ///< Stable external satellite ID selected without replacement.
};

/** Configuration or generation error raised by the F3 model. */
class F3DebrisFaultModelError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** Generate deterministic fixed-K or Poisson permanent debris-fault schedules. */
class F3DebrisFaultModel
{
  public:
    /** Construct from validated F3 parameters. */
    explicit F3DebrisFaultModel(const F3FaultParameters& parameters);

    /**
     * Generate one complete schedule using two source-specific ns-3 streams.
     *
     * fixed_k samples K independent times and K nodes without replacement.
     * poisson samples exponential inter-arrival times at the current alive
     * constellation rate, then selects one alive node uniformly.
     *
     * @param satelliteIds Complete stable-ID universe.
     * @param simulationDurationNs Exclusive simulation end in nanoseconds.
     * @param eventTimeStream Stream for fixed times or Poisson inter-arrivals.
     * @param nodeSelectionStream Stream for node selection without replacement.
     * @return Events sorted by time and then stable node ID.
     */
    std::vector<F3DebrisFaultEvent> GenerateSchedule(
        const std::vector<uint32_t>& satelliteIds,
        int64_t simulationDurationNs,
        int64_t eventTimeStream,
        int64_t nodeSelectionStream) const;

  private:
    F3FaultParameters m_parameters; ///< Frozen F3 model parameters.
};

} // namespace ns3

#endif // SATCOMPUTE_F3_DEBRIS_FAULT_MODEL_H
