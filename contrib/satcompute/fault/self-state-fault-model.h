/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SELF_STATE_FAULT_MODEL_H
#define SATCOMPUTE_SELF_STATE_FAULT_MODEL_H

#include "fault-model-config.h"

namespace ns3
{

struct SelfStateFaultSnapshot
{
    double temperatureC{};
    double depthOfDischarge{};
    double thermalRisk{};
    double energyPressure{};
    double combinedRisk{};
    double failureIntensityPerSecond{};
    double stepFailureProbability{};
    bool busy{};
};

/** Pure F1 thermal, energy, risk, and hazard calculations for one compute node. */
class SelfStateFaultModel
{
  public:
    explicit SelfStateFaultModel(const SelfStateFaultConfig& config);

    SelfStateFaultSnapshot CreateInitialSnapshot() const;
    void Update(SelfStateFaultSnapshot& snapshot,
                bool busy,
                double intervalSeconds) const;
    bool IsRiskActive(const SelfStateFaultSnapshot& snapshot) const;

  private:
    SelfStateFaultConfig m_config;
};

} // namespace ns3

#endif // SATCOMPUTE_SELF_STATE_FAULT_MODEL_H
