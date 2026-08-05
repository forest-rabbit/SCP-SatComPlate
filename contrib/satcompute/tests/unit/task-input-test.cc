/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/command-line.h"
#include "ns3/compute-profile.h"
#include "ns3/compute-task.h"
#include "ns3/task-trace.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class FakeEndpointView : public SatelliteEndpointView
{
  public:
    FakeEndpointView()
    {
        for (uint32_t satelliteId = 0; satelliteId < 4; ++satelliteId)
        {
            m_addresses.emplace(satelliteId,
                                Ipv4Address(("172.16.0." +
                                             std::to_string(satelliteId + 1))
                                                .c_str()));
        }
    }

    bool
    HasSatelliteId(uint32_t satelliteId) const override
    {
        return m_addresses.contains(satelliteId);
    }

    Ipv4Address
    GetServiceAddressBySatelliteId(uint32_t satelliteId) const override
    {
        return m_addresses.at(satelliteId);
    }

  private:
    std::map<uint32_t, Ipv4Address> m_addresses;
};

bool
SameTask(const TaskDefinition& left, const TaskDefinition& right)
{
    return left.taskId == right.taskId && left.sourceNodeId == right.sourceNodeId &&
           left.computeNodeId == right.computeNodeId &&
           left.resultNodeId == right.resultNodeId && left.inputBytes == right.inputBytes &&
           left.outputBytes == right.outputBytes &&
           left.computeWorkUnits == right.computeWorkUnits &&
           left.arrivalTimeNs == right.arrivalTimeNs &&
           left.inputTransferId == right.inputTransferId &&
           left.resultTransferId == right.resultTransferId;
}

void
ExpectComputeError(const std::filesystem::path& filename,
                   const FakeEndpointView& endpoints)
{
    try
    {
        ReadComputeProfile(filename, endpoints);
    }
    catch (const ComputeProfileError&)
    {
        return;
    }
    throw std::runtime_error("invalid compute profile was accepted: " + filename.string());
}

void
ExpectTaskError(const std::filesystem::path& filename,
                const FakeEndpointView& endpoints,
                const ComputeProfile& profile)
{
    try
    {
        ReadTaskTrace(filename, 1000000000, endpoints, profile);
    }
    catch (const TaskTraceError&)
    {
        return;
    }
    throw std::runtime_error("invalid task trace was accepted: " + filename.string());
}

void
CheckCanonicalInputs(const std::filesystem::path& fixtureRoot)
{
    const FakeEndpointView endpoints;
    const ComputeProfile firstProfile =
        ReadComputeProfile(fixtureRoot / "compute-profile-order-a.json", endpoints);
    const ComputeProfile secondProfile =
        ReadComputeProfile(fixtureRoot / "compute-profile-order-b.json", endpoints);
    Check(firstProfile.nodes.size() == 2 && secondProfile.nodes.size() == 2,
          "canonical compute-profile count differs");
    for (std::size_t index = 0; index < firstProfile.nodes.size(); ++index)
    {
        Check(firstProfile.nodes[index].nodeId == secondProfile.nodes[index].nodeId &&
                  firstProfile.nodes[index].computeRateWorkUnitsPerSecond ==
                      secondProfile.nodes[index].computeRateWorkUnitsPerSecond,
              "compute-profile array order changed canonical output");
    }
    Check(firstProfile.nodes[0].nodeId == 1 &&
              firstProfile.nodes[0].computeRateWorkUnitsPerSecond == 2000000000 &&
              GetComputeNodeProfile(firstProfile, 3).computeRateWorkUnitsPerSecond ==
                  1000000000 &&
              FindComputeNodeProfile(firstProfile, 2) == nullptr,
          "compute-profile lookup or canonical order differs");

    const TaskTrace firstTrace = ReadTaskTrace(fixtureRoot / "task-order-a.json",
                                               1000000000,
                                               endpoints,
                                               firstProfile);
    const TaskTrace secondTrace = ReadTaskTrace(fixtureRoot / "task-order-b.json",
                                                1000000000,
                                                endpoints,
                                                firstProfile);
    Check(firstTrace.tasks.size() == 2 && secondTrace.tasks.size() == 2,
          "canonical task count differs");
    for (std::size_t index = 0; index < firstTrace.tasks.size(); ++index)
    {
        Check(SameTask(firstTrace.tasks[index], secondTrace.tasks[index]),
              "task array order changed canonical output");
    }
    Check(firstTrace.tasks[0].taskId == 1 && firstTrace.tasks[0].inputTransferId == 1 &&
              firstTrace.tasks[0].resultTransferId == 2 &&
              firstTrace.tasks[1].taskId == 4 && firstTrace.tasks[1].inputTransferId == 7 &&
              firstTrace.tasks[1].resultTransferId == 8,
          "stable task transfer-ID derivation differs");

    TaskRuntime runtime(firstTrace.tasks.front());
    runtime.TransitionTo(TASK_INPUT_TRANSFERRING, 100000000, "TASK_ARRIVAL");
    runtime.TransitionTo(TASK_QUEUED, 100000010, "INPUT_TRANSFER_COMPLETE");
    runtime.TransitionTo(TASK_RUNNING, 100000020, "COMPUTE_DISPATCH");
    runtime.TransitionTo(TASK_RESULT_TRANSFERRING, 100000030, "COMPUTE_COMPLETE");
    runtime.TransitionTo(TASK_COMPLETED, 100000040, "RESULT_TRANSFER_COMPLETE");
    Check(runtime.state == TASK_COMPLETED && runtime.queueEnterTimeNs == 100000010 &&
              runtime.computeStartTimeNs == 100000020 &&
              runtime.computeCompleteTimeNs == 100000030 &&
              runtime.resultTransferCompleteTimeNs == 100000040,
          "task lifecycle timestamps differ");

    ExpectComputeError(fixtureRoot / "compute-profile-invalid-field.json", endpoints);
    ExpectComputeError(fixtureRoot / "compute-profile-duplicate.json", endpoints);
    ExpectComputeError(fixtureRoot / "compute-profile-unknown-satellite.json", endpoints);
    ExpectTaskError(fixtureRoot / "task-invalid-field.json", endpoints, firstProfile);
    ExpectTaskError(fixtureRoot / "task-duplicate.json", endpoints, firstProfile);
    ExpectTaskError(fixtureRoot / "task-missing-compute-profile.json", endpoints, firstProfile);
    ExpectTaskError(fixtureRoot / "task-invalid-stop-time.json", endpoints, firstProfile);
    ExpectTaskError(fixtureRoot / "task-invalid-endpoint.json", endpoints, firstProfile);
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string fixtureRoot;
    CommandLine command(__FILE__);
    command.AddValue("fixtureRoot", "Compute/task fixture directory", fixtureRoot);
    command.Parse(argc, argv);

    try
    {
        Check(!fixtureRoot.empty(), "fixture root is required");
        CheckCanonicalInputs(fixtureRoot);
        std::cout << "SatCompute compute/task input tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
