/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/compute-service.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

#include <cmath>
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

void
CheckFaultSafeCancellation()
{
    Recorder recorder;
    Ptr<Node> node = CreateObject<Node>();
    Ptr<ComputeService> service = CreateObject<ComputeService>();
    service->Configure(9,
                       1000000000,
                       MakeCallback(&Recorder::OnStart, &recorder),
                       MakeCallback(&Recorder::OnComplete, &recorder));
    node->AddApplication(service);
    service->SetStartTime(NanoSeconds(0));
    service->SetStopTime(NanoSeconds(1000));

    Simulator::Schedule(NanoSeconds(10), [service] {
        Check(service->SubmitTask(1, 100, Simulator::Now().GetNanoSeconds()),
              "running fault-test task was rejected");
        Check(service->SubmitTask(2, 30, Simulator::Now().GetNanoSeconds()),
              "queued fault-test task was rejected");
    });
    Simulator::Schedule(NanoSeconds(20), [service] {
        Check(service->SetComputeAvailable(false),
              "compute service did not become unavailable");
        Check(!service->SetComputeAvailable(false),
              "repeated unavailability notification was not idempotent");
        Check(service->CancelRunningTaskForFailure(1),
              "running task was not cancelled precisely");
        Check(!service->CancelRunningTaskForFailure(1),
              "running task was cancelled twice");
        Check(service->RemoveQueuedTaskForFailure(2),
              "queued task was not removed precisely");
        Check(!service->RemoveQueuedTaskForFailure(2),
              "queued task was removed twice");
        Check(!service->SubmitTask(3, 5, Simulator::Now().GetNanoSeconds()),
              "unavailable compute service accepted new work");
    });
    Simulator::Schedule(NanoSeconds(30), [service] {
        Check(service->SetComputeAvailable(true),
              "compute service did not recover");
        Check(service->SubmitTask(3, 5, Simulator::Now().GetNanoSeconds()),
              "recovered compute service rejected new work");
    });
    Simulator::Stop(NanoSeconds(100));
    Simulator::Run();

    Check(recorder.starts.size() == 2 && recorder.starts[0].taskId == 1 &&
              recorder.starts[0].timeNs == 10 && recorder.starts[1].taskId == 3 &&
              recorder.starts[1].timeNs == 30,
          "fault-safe compute dispatch history differs");
    Check(recorder.completions.size() == 1 && recorder.completions[0].taskId == 3 &&
              recorder.completions[0].timeNs == 35,
          "cancelled compute task completed or recovered work did not complete");
    Check(service->IsComputeAvailable() && service->IsIdle() &&
              service->GetEnqueuedTaskCount() == 3 &&
              service->GetCompletedTaskCount() == 1 &&
              service->GetCancelledRunningTaskCount() == 1 &&
              service->GetRemovedQueuedTaskCount() == 1 &&
              service->GetBusyTimeNs() == 5,
          "fault-safe compute counters or busy-time accounting differ");
    Simulator::Destroy();
}

void
CheckRunningTaskSnapshot()
{
    constexpr int64_t secondNs = 1000000000;
    Recorder recorder;
    Ptr<Node> node = CreateObject<Node>();
    Ptr<ComputeService> service = CreateObject<ComputeService>();
    service->Configure(11,
                       10,
                       MakeCallback(&Recorder::OnStart, &recorder),
                       MakeCallback(&Recorder::OnComplete, &recorder));
    node->AddApplication(service);
    service->SetStartTime(NanoSeconds(0));
    service->SetStopTime(NanoSeconds(20 * secondNs));

    bool idleChecked = false;
    bool progressChecked = false;
    bool completionBoundaryChecked = false;
    bool completedChecked = false;
    Simulator::Schedule(NanoSeconds(secondNs / 2),
                        [service, &idleChecked] {
                            Check(!service->GetRunningTaskSnapshot().has_value(),
                                  "idle compute service exposed a running task");
                            idleChecked = true;
                        });
    Simulator::Schedule(NanoSeconds(secondNs), &Submit, service, 41, 100);
    Simulator::Schedule(NanoSeconds(4 * secondNs),
                        [service, &progressChecked] {
                            const auto snapshot = service->GetRunningTaskSnapshot();
                            Check(snapshot.has_value() && snapshot->taskId == 41 &&
                                      snapshot->startTimeNs == 1000000000 &&
                                      snapshot->serviceTimeNs == 10000000000 &&
                                      snapshot->elapsedTimeNs == 3000000000 &&
                                      snapshot->remainingTimeNs == 7000000000 &&
                                      std::abs(snapshot->completionRatio - 0.3) < 1e-15,
                                  "running compute task progress snapshot differs");
                            progressChecked = true;
                        });
    // This event was scheduled before the dynamic completion event, so it
    // observes the causal boundary immediately before task completion.
    Simulator::Schedule(NanoSeconds(11 * secondNs),
                        [service, &completionBoundaryChecked] {
                            const auto snapshot = service->GetRunningTaskSnapshot();
                            Check(snapshot.has_value() &&
                                      snapshot->elapsedTimeNs == snapshot->serviceTimeNs &&
                                      snapshot->remainingTimeNs == 0 &&
                                      snapshot->completionRatio == 1.0,
                                  "compute completion boundary snapshot differs");
                            completionBoundaryChecked = true;
                        });
    Simulator::Schedule(NanoSeconds(12 * secondNs),
                        [service, &completedChecked] {
                            Check(!service->GetRunningTaskSnapshot().has_value(),
                                  "completed compute task remained visible");
                            completedChecked = true;
                        });
    Simulator::Stop(NanoSeconds(13 * secondNs));
    Simulator::Run();

    Check(idleChecked && progressChecked && completionBoundaryChecked &&
              completedChecked && recorder.completions.size() == 1 &&
              recorder.completions[0].timeNs == 11 * secondNs,
          "running task snapshot checks did not complete");
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
        CheckFaultSafeCancellation();
        CheckRunningTaskSnapshot();
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
