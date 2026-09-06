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
    /** Bind existing candidate devices before Simulator::Run; topology outlives this object. */
    LinkMetricsRecorder(SatelliteTopology& topology, Ptr<NetworkTransferEngine> engine,
                        int64_t durationNs, int64_t intervalNs,
                        const std::filesystem::path& directory);
    ~LinkMetricsRecorder();
    /** Flush idle/tail intervals after Simulator::Run and write per-link totals. */
    void Finalize();
    /** Remove only this recorder's known outputs when collection is disabled. */
    static void RemoveOutputs(const std::filesystem::path& directory);

  private:
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

    static void OnTx(Entry* entry, Ptr<const Packet> packet);
    static void OnDrop(Entry* entry, Ptr<const Packet> packet);
    static void OnQueue(Entry* entry, uint32_t oldBytes, uint32_t newBytes);
    void OnReservation(uint32_t source, uint32_t interface, uint64_t rate);
    void OnTopologyUpdate();
    bool BeforeEvent();
    void FlushThrough(int64_t nowNs);
    void WriteWindow(int64_t endNs);
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
