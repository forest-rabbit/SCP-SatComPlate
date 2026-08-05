/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "network-transfer-records.h"

namespace ns3
{

const char*
TransferRuntimeStateToString(TransferRuntimeState state)
{
    switch (state)
    {
    case TransferRuntimeState::REGISTERED:
        return "REGISTERED";
    case TransferRuntimeState::WAITING_ADMISSION:
        return "WAITING_ADMISSION";
    case TransferRuntimeState::ACTIVE:
        return "ACTIVE";
    case TransferRuntimeState::PAUSED_ROUTE:
        return "PAUSED_ROUTE";
    case TransferRuntimeState::SENDER_FINISHED:
        return "SENDER_FINISHED";
    case TransferRuntimeState::COMPLETED:
        return "COMPLETED";
    case TransferRuntimeState::FAILED:
        return "FAILED";
    case TransferRuntimeState::CANCELLED:
        return "CANCELLED";
    }
    return "UNKNOWN";
}

const char*
TransferTerminalReasonToString(TransferTerminalReason reason)
{
    switch (reason)
    {
    case TransferTerminalReason::RECEIVER_COMPLETED:
        return "RECEIVER_COMPLETED";
    case TransferTerminalReason::SOURCE_SATELLITE_FAILED:
        return "SOURCE_SATELLITE_FAILED";
    case TransferTerminalReason::DESTINATION_SATELLITE_FAILED:
        return "DESTINATION_SATELLITE_FAILED";
    case TransferTerminalReason::TASK_FAILED:
        return "TASK_FAILED";
    case TransferTerminalReason::TASK_NO_LONGER_REQUIRES_TRANSFER:
        return "TASK_NO_LONGER_REQUIRES_TRANSFER";
    case TransferTerminalReason::COMPUTE_NODE_FAILED:
        return "COMPUTE_NODE_FAILED";
    case TransferTerminalReason::SIMULATION_ENDED:
        return "SIMULATION_ENDED";
    }
    return "UNKNOWN";
}

bool
IsTerminalTransferState(TransferRuntimeState state)
{
    return state == TransferRuntimeState::COMPLETED ||
           state == TransferRuntimeState::FAILED ||
           state == TransferRuntimeState::CANCELLED;
}

} // namespace ns3
