/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_COMPUTE_USAGE_HISTORY_H
#define SATCOMPUTE_COMPUTE_USAGE_HISTORY_H
#include <cstdint>
#include <map>
#include <vector>

namespace ns3::protection
{
/** Actual service in one past-only observation window. */
struct ComputeUsageWindow
{
    uint64_t normalNs{}, recoveryNs{}, exposureNs{}; ///< Exact durations inside the queried window.
};

/** Passive event-prefix integrals; no service ownership, timers or predictions. */
class ComputeUsageHistory
{
  public:
    enum class Activity { IDLE, NORMAL, RECOVERY }; ///< Service state, never a reservation state.
    /** Observe the post-transition activity at an exact simulation timestamp.
     * @param node Stable satellite ID.
     * @param timeNs Nondecreasing observation time.
     * @param activity Actual service state after this notification.
     */
    void Observe(uint32_t node, int64_t timeNs, Activity activity);
    /** Query [begin,end), using only events observed by asOf and observed live exposure.
     * Nodes exist from t=0; live exposure ends only at an already observed permanent F3.
     * @param node Stable satellite ID observed since t=0.
     * @param beginNs Inclusive window start.
     * @param endNs Exclusive window end.
     * @param asOfNs Current causal observation time, not earlier than endNs.
     * @param survivalExposureNs Cumulative live exposure observed by asOfNs.
     * @return Normal/recovery actual busy time and matching live exposure.
     */
    ComputeUsageWindow Query(uint32_t node, int64_t beginNs, int64_t endNs,
                             int64_t asOfNs, uint64_t survivalExposureNs) const;
  private:
    /** Cumulative exact integrals and activity immediately after one state transition. */
    struct Prefix
    {
        int64_t timeNs{}; ///< Transition time.
        uint64_t normalNs{}, recoveryNs{}; ///< Service integrals through timeNs.
        Activity activity{Activity::IDLE}; ///< Activity from timeNs to the next transition.
    };
    /** Integrate unchanged activity. @param prefix Previous prefix. @param timeNs Query end.
     * @return Prefix integrated to timeNs.
     */
    static Prefix Advance(Prefix prefix, int64_t timeNs);
    /** Read a causal prefix. @param node Stable ID. @param timeNs Query time.
     * @return Prefix without an event later than timeNs.
     */
    Prefix At(uint32_t node, int64_t timeNs) const;
    std::map<uint32_t, std::vector<Prefix>> m_events; ///< Per-node sorted activity transitions.
    std::map<uint32_t, int64_t> m_lastObservedNs; ///< Includes repeated state notifications.
};
} // namespace ns3::protection
#endif
