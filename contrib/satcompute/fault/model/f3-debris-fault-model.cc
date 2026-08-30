/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "f3-debris-fault-model.h"

#include "ns3/random-variable-stream.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ns3
{

namespace
{

uint32_t
SelectAndRemoveNode(std::vector<uint32_t>& remaining,
                    const Ptr<UniformRandomVariable>& random)
{
    const uint32_t maximumIndex = static_cast<uint32_t>(remaining.size() - 1);
    const uint32_t index = random->GetInteger(0, maximumIndex);
    const uint32_t nodeId = remaining[index];
    remaining.erase(remaining.begin() + index);
    return nodeId;
}

int64_t
SampleFixedTime(const Ptr<UniformRandomVariable>& random,
                int64_t simulationDurationNs)
{
    const double sampled = random->GetValue(
        0.0,
        static_cast<double>(simulationDurationNs));
    const int64_t timeNs = static_cast<int64_t>(std::floor(sampled));
    return std::min(timeNs, simulationDurationNs - 1);
}

void
SortEvents(std::vector<F3DebrisFaultEvent>& events)
{
    std::sort(events.begin(),
              events.end(),
              [](const F3DebrisFaultEvent& left,
                 const F3DebrisFaultEvent& right) {
                  return std::make_pair(left.startTimeNs, left.nodeId) <
                         std::make_pair(right.startTimeNs, right.nodeId);
              });
}

} // namespace

F3DebrisFaultModel::F3DebrisFaultModel(const F3FaultParameters& parameters)
    : m_parameters(parameters)
{
    if (!parameters.enabled)
    {
        throw F3DebrisFaultModelError("F3 model requires enabled parameters");
    }
    if (parameters.mode != "fixed_k" && parameters.mode != "poisson")
    {
        throw F3DebrisFaultModelError("F3 mode must be fixed_k or poisson");
    }
}

std::vector<F3DebrisFaultEvent>
F3DebrisFaultModel::GenerateSchedule(
    const std::vector<uint32_t>& satelliteIds,
    int64_t simulationDurationNs,
    int64_t eventTimeStream,
    int64_t nodeSelectionStream) const
{
    if (satelliteIds.empty() || simulationDurationNs <= 0 ||
        eventTimeStream < 0 || nodeSelectionStream < 0)
    {
        throw F3DebrisFaultModelError(
            "F3 schedule requires satellites, duration, and non-negative streams");
    }
    std::vector<uint32_t> remaining = satelliteIds;
    std::sort(remaining.begin(), remaining.end());
    if (std::adjacent_find(remaining.begin(), remaining.end()) != remaining.end())
    {
        throw F3DebrisFaultModelError("F3 satellite IDs must be unique");
    }
    if (m_parameters.mode == "fixed_k" &&
        m_parameters.fixedCount > remaining.size())
    {
        throw F3DebrisFaultModelError(
            "F3 fixed_count exceeds the satellite count");
    }

    Ptr<UniformRandomVariable> nodeRandom =
        CreateObject<UniformRandomVariable>();
    nodeRandom->SetStream(nodeSelectionStream);
    std::vector<F3DebrisFaultEvent> events;
    if (m_parameters.mode == "fixed_k")
    {
        Ptr<UniformRandomVariable> timeRandom =
            CreateObject<UniformRandomVariable>();
        timeRandom->SetStream(eventTimeStream);
        std::vector<int64_t> times;
        times.reserve(m_parameters.fixedCount);
        for (uint32_t index = 0; index < m_parameters.fixedCount; ++index)
        {
            times.push_back(SampleFixedTime(timeRandom, simulationDurationNs));
        }
        std::sort(times.begin(), times.end());
        events.reserve(m_parameters.fixedCount);
        for (const int64_t timeNs : times)
        {
            events.push_back(
                {timeNs, SelectAndRemoveNode(remaining, nodeRandom)});
        }
        SortEvents(events);
        return events;
    }

    if (!std::isfinite(m_parameters.singleSatelliteIntensityPerSecond) ||
        m_parameters.singleSatelliteIntensityPerSecond <= 0.0)
    {
        throw F3DebrisFaultModelError(
            "F3 poisson intensity must be finite and positive");
    }
    Ptr<ExponentialRandomVariable> timeRandom =
        CreateObject<ExponentialRandomVariable>();
    timeRandom->SetStream(eventTimeStream);
    constexpr long double NANOSECONDS_PER_SECOND = 1000000000.0L;
    long double currentTimeNs = 0.0L;
    while (!remaining.empty())
    {
        const double constellationIntensity =
            static_cast<double>(remaining.size()) *
            m_parameters.singleSatelliteIntensityPerSecond;
        const double deltaSeconds =
            timeRandom->GetValue(1.0 / constellationIntensity, 0.0);
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
        {
            throw F3DebrisFaultModelError(
                "F3 poisson inter-arrival sample is invalid");
        }
        currentTimeNs +=
            static_cast<long double>(deltaSeconds) * NANOSECONDS_PER_SECOND;
        if (currentTimeNs >= static_cast<long double>(simulationDurationNs))
        {
            break;
        }
        const int64_t timeNs = static_cast<int64_t>(std::floor(currentTimeNs));
        events.push_back(
            {timeNs, SelectAndRemoveNode(remaining, nodeRandom)});
    }
    SortEvents(events);
    return events;
}

} // namespace ns3
