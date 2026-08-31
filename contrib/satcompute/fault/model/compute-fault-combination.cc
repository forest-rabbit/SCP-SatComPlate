/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "compute-fault-combination.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace ns3
{

namespace
{

void
RequireProbability(double value, const char* name)
{
    if (!std::isfinite(value) || value < 0.0 || value > 1.0)
    {
        throw std::invalid_argument(std::string(name) + " must be finite and in [0, 1]");
    }
}

void
RequireRandomValue(double value, const char* name)
{
    if (!std::isfinite(value) || value < 0.0 || value >= 1.0)
    {
        throw std::invalid_argument(std::string(name) + " must be finite and in [0, 1)");
    }
}

} // namespace

double
CombineComputeFaultProbabilities(double f1Probability,
                                 double f2Probability)
{
    RequireProbability(f1Probability, "F1 probability");
    RequireProbability(f2Probability, "F2 probability");
    return 1.0 - (1.0 - f1Probability) * (1.0 - f2Probability);
}

ComputeFaultSourceOutcome
EvaluateComputeFaultSources(double f1Probability,
                            double f1RandomValue,
                            double f2Probability,
                            double f2RandomValue)
{
    RequireProbability(f1Probability, "F1 probability");
    RequireRandomValue(f1RandomValue, "F1 random value");
    RequireProbability(f2Probability, "F2 probability");
    RequireRandomValue(f2RandomValue, "F2 random value");

    ComputeFaultSourceOutcome outcome;
    outcome.combinedProbability =
        CombineComputeFaultProbabilities(f1Probability, f2Probability);
    outcome.f1Occurred = f1RandomValue < f1Probability;
    outcome.f2Occurred = f2RandomValue < f2Probability;
    outcome.computeFaultOccurred = outcome.f1Occurred || outcome.f2Occurred;
    return outcome;
}

} // namespace ns3
