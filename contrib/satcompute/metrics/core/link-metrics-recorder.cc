/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "link-metrics-recorder.h"

#include "ns3/ipv4.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <iomanip>
#include <stdexcept>

namespace ns3
{
namespace
{
constexpr const char* kColumns =
    "available_time_s,tx_busy_time_s,available_tx_busy_time_s,tx_started_bytes,"
    "tx_started_packets,serialized_bits,mean_link_throughput_bps,utilization_percent,"
    "available_utilization_percent,mean_link_capacity_bps,mean_available_capacity_bps,mean_reserved_rate_bps,"
    "peak_reserved_rate_bps,mean_queue_bytes,max_queue_bytes,drop_packets,drop_bytes";

void WriteTotals(std::ostream& out, const LinkWindowTotals& t, int64_t durationNs)
{
    out << t.availableNs / 1e9L << ',' << t.busyNs / 1e9L << ','
        << t.availableBusyNs / 1e9L << ',' << t.txBytes << ',' << t.txPackets << ','
        << t.serializedBits << ',' << t.serializedBits * 1e9L / durationNs << ','
        << t.busyNs * 100.0L / durationNs << ',';
    if (t.availableNs > 0)
    {
        out << t.availableBusyNs * 100.0L / t.availableNs;
    }
    out << ',' << t.capacityBitNs / durationNs << ',' << t.availableCapacityBitNs / durationNs << ','
        << t.reservedBitNs / durationNs << ',' << t.peakReservedBps << ','
        << t.queueByteNs / durationNs << ',' << t.maxQueueBytes << ','
        << t.dropPackets << ',' << t.dropBytes;
}
}

uint64_t
LinkMetricsRecorder::ReadRate(Ptr<PointToPointNetDevice> device)
{
    DataRateValue rate;
    device->GetAttribute("DataRate", rate);
    return rate.Get().GetBitRate();
}

LinkMetricsRecorder::LinkMetricsRecorder(SatelliteTopology& topology,
    Ptr<NetworkTransferEngine> engine, int64_t durationNs, int64_t intervalNs,
    const std::filesystem::path& directory)
    : m_topology(topology), m_engine(engine), m_directory(directory),
      m_durationNs(durationNs), m_intervalNs(intervalNs)
{
    if (durationNs <= 0 || intervalNs <= 0 || Simulator::Now().GetNanoSeconds() != 0)
    {
        throw std::invalid_argument("link metrics require positive durations and time zero");
    }
    std::filesystem::create_directories(directory);
    m_windows.open(directory / "link-window-metrics.csv");
    m_network.open(directory / "network-link-window-metrics.csv");
    if (!m_windows || !m_network)
    {
        throw std::runtime_error("cannot open link metric outputs");
    }
    m_windows << std::setprecision(18)
              << "window_start_s,window_end_s,source_node_id,destination_node_id,"
                 "output_interface," << kColumns << '\n';
    m_network << std::setprecision(18)
              << "window_start_s,window_end_s,directed_link_count,available_link_time_s,"
                 "available_busy_time_s,mean_utilization_percent,max_link_utilization_percent,"
                 "serialized_bits,sum_link_throughput_bps,drop_packets,drop_bytes\n";

    for (const auto& link : topology.GetIslDirectedLinks())
    {
        const auto ipv4 = topology.GetNodeBySatelliteId(link.sourceSatelliteId)->GetObject<Ipv4>();
        const auto device = DynamicCast<PointToPointNetDevice>(ipv4->GetNetDevice(link.outputInterface));
        if (!device)
        {
            throw std::runtime_error("link metrics require point-to-point devices");
        }
        const bool active = topology.GetLinkState().IsLinkActive(link.sourceSatelliteId,
                                                                link.destinationSatelliteId);
        auto entry = std::make_unique<Entry>(Entry{this, link, device,
                                                   LinkWindow(ReadRate(device), active), {}});
        entry->txCallback = MakeBoundCallback(&OnTx, entry.get());
        entry->dropCallback = MakeBoundCallback(&OnDrop, entry.get());
        entry->queueCallback = MakeBoundCallback(&OnQueue, entry.get());
        const auto queue = device->GetQueue();
        if (!device->TraceConnectWithoutContext("PhyTxBegin", entry->txCallback) ||
            !queue->TraceConnectWithoutContext("Drop", entry->dropCallback) ||
            !queue->TraceConnectWithoutContext("BytesInQueue", entry->queueCallback))
        {
            throw std::runtime_error("cannot connect link metric trace");
        }
        entry->window.SetQueue(0, queue->GetNBytes());
        m_entries.emplace(std::make_pair(link.sourceSatelliteId, link.outputInterface),
                          std::move(entry));
    }
    topology.RegisterRouteUpdateCallback(MakeCallback(&LinkMetricsRecorder::OnTopologyUpdate, this));
    if (m_engine)
    {
        m_engine->SetCapacityReservationObserver(MakeCallback(&LinkMetricsRecorder::OnReservation, this));
    }
}

LinkMetricsRecorder::~LinkMetricsRecorder()
{
    if (m_engine)
    {
        m_engine->SetCapacityReservationObserver({});
    }
    for (auto& [key, entry] : m_entries)
    {
        entry->device->TraceDisconnectWithoutContext("PhyTxBegin", entry->txCallback);
        entry->device->GetQueue()->TraceDisconnectWithoutContext("Drop", entry->dropCallback);
        entry->device->GetQueue()->TraceDisconnectWithoutContext("BytesInQueue", entry->queueCallback);
    }
}

bool
LinkMetricsRecorder::BeforeEvent()
{
    if (m_finalized)
    {
        return false;
    }
    const int64_t now = Simulator::Now().GetNanoSeconds();
    FlushThrough(now);
    return now < m_durationNs;
}

void
LinkMetricsRecorder::OnTx(Entry* entry, Ptr<const Packet> packet)
{
    if (entry->owner->BeforeEvent())
    {
        const int64_t now = Simulator::Now().GetNanoSeconds();
        const auto rate = DataRate(ReadRate(entry->device));
        entry->window.StartTransmission(now, packet->GetSize(),
                                        rate.CalculateBytesTxTime(packet->GetSize()).GetNanoSeconds());
    }
}

void
LinkMetricsRecorder::OnDrop(Entry* entry, Ptr<const Packet> packet)
{
    if (entry->owner->BeforeEvent())
    {
        entry->window.Drop(Simulator::Now().GetNanoSeconds(), packet->GetSize());
    }
}

void
LinkMetricsRecorder::OnQueue(Entry* entry, uint32_t, uint32_t newBytes)
{
    if (entry->owner->BeforeEvent())
    {
        entry->window.SetQueue(Simulator::Now().GetNanoSeconds(), newBytes);
    }
}

void
LinkMetricsRecorder::OnReservation(uint32_t source, uint32_t interface, uint64_t rate)
{
    if (BeforeEvent())
    {
        m_entries.at({source, interface})->window.SetReserved(Simulator::Now().GetNanoSeconds(), rate);
    }
}

void
LinkMetricsRecorder::OnTopologyUpdate()
{
    if (!BeforeEvent())
    {
        return;
    }
    for (auto& [key, entry] : m_entries)
    {
        const auto& link = entry->link;
        entry->window.SetLink(Simulator::Now().GetNanoSeconds(), ReadRate(entry->device),
            m_topology.GetLinkState().IsLinkActive(link.sourceSatelliteId, link.destinationSatelliteId));
    }
}

void
LinkMetricsRecorder::FlushThrough(int64_t nowNs)
{
    while (m_startNs < m_durationNs)
    {
        const int64_t next = m_startNs + std::min(m_intervalNs, m_durationNs - m_startNs);
        if (next > nowNs)
        {
            break;
        }
        WriteWindow(next);
        m_startNs = next;
    }
}

void
LinkMetricsRecorder::WriteWindow(int64_t endNs)
{
    const int64_t duration = endNs - m_startNs;
    LinkWindowTotals network;
    double peak = 0;
    for (auto& [key, entry] : m_entries)
    {
        const auto totals = entry->window.Take(endNs);
        entry->total.Add(totals);
        network.Add(totals);
        const double utilization = totals.busyNs * 100.0 / duration;
        entry->peakUtilization = std::max(entry->peakUtilization, utilization);
        peak = std::max(peak, utilization);
        m_windows << m_startNs / 1e9L << ',' << endNs / 1e9L << ','
                  << entry->link.sourceSatelliteId << ',' << entry->link.destinationSatelliteId
                  << ',' << entry->link.outputInterface << ',';
        WriteTotals(m_windows, totals, duration);
        m_windows << '\n';
    }
    m_network << m_startNs / 1e9L << ',' << endNs / 1e9L << ',' << m_entries.size()
              << ',' << network.availableNs / 1e9L << ',' << network.availableBusyNs / 1e9L << ',';
    if (network.availableNs > 0)
    {
        m_network << network.availableBusyNs * 100.0L / network.availableNs;
    }
    m_network << ',' << peak << ',' << network.serializedBits << ','
              << network.serializedBits * 1e9L / duration << ',' << network.dropPackets
              << ',' << network.dropBytes << '\n';
    if (!m_windows || !m_network)
    {
        throw std::runtime_error("cannot write link metric window");
    }
}

void
LinkMetricsRecorder::Finalize()
{
    if (m_finalized)
    {
        return;
    }
    FlushThrough(m_durationNs);
    std::ofstream summary(m_directory / "link-summary.csv");
    summary << std::setprecision(18)
            << "source_node_id,destination_node_id,output_interface,measurement_duration_s,"
            << kColumns << ",peak_window_utilization_percent\n";
    for (const auto& [key, entry] : m_entries)
    {
        summary << entry->link.sourceSatelliteId << ',' << entry->link.destinationSatelliteId
                << ',' << entry->link.outputInterface << ',' << m_durationNs / 1e9L << ',';
        WriteTotals(summary, entry->total, m_durationNs);
        summary << ',' << entry->peakUtilization << '\n';
    }
    summary.close();
    m_windows.close();
    m_network.close();
    if (!summary || !m_windows || !m_network)
    {
        throw std::runtime_error("cannot finalize link metric outputs");
    }
    m_finalized = true;
}

void
LinkMetricsRecorder::RemoveOutputs(const std::filesystem::path& directory)
{
    for (const auto* name : {"link-window-metrics.csv", "network-link-window-metrics.csv",
                             "link-summary.csv"})
    {
        std::filesystem::remove(directory / name);
    }
}

} // namespace ns3
