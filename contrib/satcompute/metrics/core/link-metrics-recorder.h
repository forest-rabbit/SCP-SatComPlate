/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_LINK_METRICS_RECORDER_H
#define SATCOMPUTE_LINK_METRICS_RECORDER_H

#include "link-window.h"
#include "../../topology/satellite-topology.h"
#include "../../traffic/network-transfer-engine.h"

#include "ns3/point-to-point-net-device.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

namespace ns3
{

/** Optional streaming link metrics; schedules no simulator events. */
class LinkMetricsRecorder
{
  public:
    /** Bind existing candidate devices before Simulator::Run.
     * Keep the recorder alive through all simulator/topology updates; after its
     * destruction the topology must not issue further route-update callbacks.
     * @param topology Observed topology, which must outlive this recorder.
     * @param engine Optional source of capacity reservation observations.
     * @param durationNs Positive observation horizon in nanoseconds.
     * @param intervalNs Positive window size in nanoseconds.
     * @param directory Directory for the three link metric outputs.
     */
    LinkMetricsRecorder(SatelliteTopology& topology, Ptr<NetworkTransferEngine> engine,
                        int64_t durationNs, int64_t intervalNs,
                        const std::filesystem::path& directory);
    /** Disconnect device and reservation observations after simulation ends. */
    ~LinkMetricsRecorder();
    /** Flush idle/tail intervals after Simulator::Run and write per-link totals. */
    void Finalize();
    /** Remove only this recorder's known outputs when collection is disabled.
     * @param directory Output directory; unrelated files are preserved.
     */
    static void RemoveOutputs(const std::filesystem::path& directory);

  private:
    /** Bound trace callbacks and accumulated state for one directed device. */
    struct Entry
    {
        LinkMetricsRecorder* owner; ///< Owning recorder.
        IslDirectedLink link; ///< Stable satellite and IPv4 interface identity.
        Ptr<PointToPointNetDevice> device; ///< Observed physical device.
        LinkWindow window; ///< Current interval state.
        LinkWindowTotals total; ///< Whole-run integrals.
        double peakUtilization{}; ///< Largest window utilization, percent.
        Callback<void, Ptr<const Packet>> txCallback; ///< Bound physical transmit trace.
        Callback<void, Ptr<const Packet>> dropCallback; ///< Bound queue drop trace.
        Callback<void, uint32_t, uint32_t> queueCallback; ///< Bound queue occupancy trace.
    };

    /** Observe serialization start.
     * @param entry Directed link state.
     * @param packet Complete frame being sent.
     */
    static void OnTx(Entry* entry, Ptr<const Packet> packet);
    /** Observe a queue rejection.
     * @param entry Directed link state.
     * @param packet Dropped frame.
     */
    static void OnDrop(Entry* entry, Ptr<const Packet> packet);
    /** Observe queue occupancy.
     * @param entry Directed link state.
     * @param oldBytes Previous queued byte count.
     * @param newBytes Current queued byte count.
     */
    static void OnQueue(Entry* entry, uint32_t oldBytes, uint32_t newBytes);
    /** Observe post-change capacity reservation.
     * @param source External source satellite ID.
     * @param interface IPv4 output interface index.
     * @param rate New reserved rate in bit/s.
     */
    void OnReservation(uint32_t source, uint32_t interface, uint64_t rate);
    /** Refresh logical link availability after a topology transition. */
    void OnTopologyUpdate();
    /** Close elapsed windows before recording a same-time event.
     * @return Whether the event is inside the observation horizon.
     */
    bool BeforeEvent();
    /** Write all complete windows ending by the supplied time.
     * @param nowNs Inclusive flush limit in nanoseconds.
     */
    void FlushThrough(int64_t nowNs);
    /** Write one per-link/network window and update whole-run totals.
     * @param endNs Exclusive window end in nanoseconds.
     */
    void WriteWindow(int64_t endNs);
    /** Read the native device rate without narrowing to 32 bits.
     * @param device Observed point-to-point device.
     * @return Configured bit/s.
     */
    static uint64_t ReadRate(Ptr<PointToPointNetDevice> device);

    SatelliteTopology& m_topology; ///< Observed topology, never mutated.
    Ptr<NetworkTransferEngine> m_engine; ///< Optional reservation source.
    std::map<std::pair<uint32_t, uint32_t>, std::unique_ptr<Entry>> m_entries; ///< Directed links.
    std::filesystem::path m_directory; ///< Output directory.
    std::ofstream m_windows; ///< Streaming per-link window CSV.
    std::ofstream m_network; ///< Streaming network window CSV.
    int64_t m_durationNs; ///< Full observation horizon.
    int64_t m_intervalNs; ///< Requested window duration.
    int64_t m_startNs{}; ///< Start of the not-yet-written window.
    bool m_finalized{}; ///< Finalization guard.
};

} // namespace ns3

#endif
