/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAULT_DEFINITION_H
#define SATCOMPUTE_FAULT_DEFINITION_H

#include <cstdint>
#include <optional>

namespace ns3
{

enum class FaultType
{
    COMPUTE,
    SATELLITE
};

const char* FaultTypeToString(FaultType type);

/** One deterministic input fault; probability is metadata, not a runtime draw. */
struct FaultDefinition
{
    uint64_t faultId{};
    uint32_t nodeId{};
    FaultType faultType{FaultType::COMPUTE};
    int64_t startTimeNs{};
    std::optional<int64_t> noticeTimeNs;
    std::optional<double> failureProbability;
    std::optional<int64_t> durationNs;

    std::optional<int64_t> GetRecoveryTimeNs() const;
    std::optional<int64_t> GetWarningLeadTimeNs() const;
};

} // namespace ns3

#endif // SATCOMPUTE_FAULT_DEFINITION_H
