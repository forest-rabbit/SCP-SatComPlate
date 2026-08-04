/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/compute-service.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

struct TaskEvent
{
    uint64_t taskId;
    uint32_t nodeId;
    int64_t timeNs;
};

class Recorder
{
  public:
    void
    OnStart(uint64_t taskId, uint32_t nodeId, int64_t timeNs)
    {
        starts.push_back({taskId, nodeId, timeNs});
    }

    void
    OnComplete(uint64_t taskId, uint32_t nodeId, int64_t timeNs)
    {
        completions.push_back({taskId, nodeId, timeNs});
    }

    std::vector<TaskEvent> starts;
    std::vector<TaskEvent> completions;
};

void
Submit(Ptr<ComputeService> service, uint64_t taskId, uint64_t workUnits)
{
    service->SubmitTask(taskId, workUnits, Simulator::Now().GetNanoSeconds());
}

void
CheckExactServiceTime()
{
    Check(ComputeService::CalculateServiceTimeNs(1, 3) == 333333334,
          "fractional service time did not round up");
    Check(ComputeService::CalculateServiceTimeNs(3, 2000000000) == 2,
          "sub-nanosecond fraction did not round up");
    Check(ComputeService::CalculateServiceTimeNs(9, 1000000000) == 9,
          "integral service time differs");
}

void
CheckFcfsQueue()
{
    Recorder recorder;
    Ptr<Node> node = CreateObject<Node>();
    Ptr<ComputeService> service = CreateObject<ComputeService>();
    service->Configure(7,
                       1000000000,
                       MakeCallback(&Recorder::OnStart, &recorder),
                       MakeCallback(&Recorder::OnComplete, &recorder));
    node->AddApplication(service);
    service->SetStartTime(NanoSeconds(0));
    service->SetStopTime(NanoSeconds(50));

    // Deliberately submit IDs out of order in the same nanosecond. ScheduleNow
    // dispatch lets all three enter the queue before the stable tie-break.
    Simulator::Schedule(NanoSeconds(10), &Submit, service, 30, 3);
    Simulator::Schedule(NanoSeconds(10), &Submit, service, 10, 1);
    Simulator::Schedule(NanoSeconds(10), &Submit, service, 20, 2);
    Simulator::Stop(NanoSeconds(50));
    Simulator::Run();

    Check(recorder.starts.size() == 3 && recorder.completions.size() == 3,
          "compute callback count differs");
    Check(recorder.starts[0].taskId == 10 && recorder.starts[0].timeNs == 10 &&
              recorder.starts[1].taskId == 20 && recorder.starts[1].timeNs == 11 &&
              recorder.starts[2].taskId == 30 && recorder.starts[2].timeNs == 13,
          "same-nanosecond FCFS order or non-preemptive start times differ");
    Check(recorder.completions[0].taskId == 10 &&
              recorder.completions[0].timeNs == 11 &&
              recorder.completions[1].taskId == 20 &&
              recorder.completions[1].timeNs == 13 &&
              recorder.completions[2].taskId == 30 &&
              recorder.completions[2].timeNs == 16,
          "compute completion order or exact duration differs");
    for (const TaskEvent& event : recorder.starts)
    {
        Check(event.nodeId == 7, "compute callback node ID differs");
    }
    Check(service->GetNodeId() == 7 &&
              service->GetComputeRateWorkUnitsPerSecond() == 1000000000 &&
              service->GetEnqueuedTaskCount() == 3 &&
              service->GetCompletedTaskCount() == 3 && service->GetBusyTimeNs() == 6 &&
              service->GetMaxQueueLength() == 3 && service->IsIdle(),
          "compute service metrics differ");
    Simulator::Destroy();
}

} // namespace

int
main()
{
    try
    {
        CheckExactServiceTime();
        CheckFcfsQueue();
        std::cout << "SatCompute compute service tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        Simulator::Destroy();
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
