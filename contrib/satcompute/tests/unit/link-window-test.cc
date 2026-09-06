/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/link-window.h"

#include <cmath>
#include <iostream>
#include <source_location>
#include <stdexcept>

using namespace ns3;

namespace
{
void Check(bool condition, std::source_location location = std::source_location::current())
{
    if (!condition)
    {
        throw std::runtime_error("link-window assertion failed at line " +
                                 std::to_string(location.line()));
    }
}
}

int main()
{
    LinkWindow idle(10000000000ULL, true);
    const auto empty = idle.Take(1000000000);
    Check(empty.busyNs == 0 && empty.txBytes == 0 && empty.availableNs == 1000000000);
    Check(empty.availableCapacityBitNs == 10000000000000000000.0L);
    Check(empty.capacityBitNs == empty.availableCapacityBitNs);

    LinkWindow link(8000, true);
    link.SetQueue(100000000, 100);
    link.SetReserved(200000000, 4000);
    link.StartTransmission(750000000, 500, 500000000);
    link.Drop(900000000, 70);
    const auto first = link.Take(1000000000);
    Check(first.busyNs == 250000000 && std::abs(first.serializedBits - 2000) < 1e-9L);
    Check(first.txBytes == 500 && first.txPackets == 1 && first.dropBytes == 70);
    Check(first.queueByteNs == 90000000000.0L && first.maxQueueBytes == 100);
    Check(first.reservedBitNs == 3200000000000.0L && first.peakReservedBps == 4000);

    link.SetLink(1100000000, 16000, false);
    link.SetQueue(1200000000, 0);
    link.SetReserved(1200000000, 0);
    const auto tail = link.Take(1500000000);
    Check(tail.busyNs == 250000000 && std::abs(tail.serializedBits - 2000) < 1e-9L);
    Check(tail.availableNs == 100000000 && tail.availableBusyNs == 100000000);
    Check(tail.txBytes == 0 && tail.queueByteNs == 20000000000.0L);
    Check(tail.reservedBitNs == 800000000000.0L);
    const auto down = link.Take(2000000000);
    Check(down.availableNs == 0 && down.busyNs == 0 && down.maxQueueBytes == 0);
    link.SetLink(2000000000, 16000, true);
    Check(link.Take(2500000000).availableCapacityBitNs == 8000000000000.0L);

    LinkWindow reverse(8000, true);
    Check(reverse.Take(2500000000).busyNs == 0);
    LinkWindow boundary(8000, true);
    Check(boundary.Take(1000000000).txPackets == 0);
    boundary.StartTransmission(1000000000, 1000, 1000000000);
    Check(boundary.Take(2000000000).busyNs == 1000000000);
    std::cout << "Link window checks passed\n";
}
