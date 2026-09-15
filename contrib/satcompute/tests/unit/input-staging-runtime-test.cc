/* SPDX-License-Identifier: GPL-2.0-only */
#include "../support/config-factory.h"
#include "../../protection/mechanism/input-staging/input-staging-manager.h"
#include "ns3/checkpoint-manager.h"
#include "ns3/command-line.h"
#include "ns3/fault-controller.h"
#include "ns3/fixed-protection-policy.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/mac48-address.h"
#include "ns3/online-topology-controller.h"
#include "ns3/protection-metrics.h"
#include "ns3/recovery-controller.h"
#include "../../protection/storage/peak-quota-ledger.h"
#include "ns3/simulator.h"
#include <iostream>
#include <stdexcept>

using namespace ns3;
using namespace ns3::protection;
namespace
{
uint64_t checks{};
void Check(bool condition, const std::string& reason)
{ ++checks; if (!condition) throw std::runtime_error(reason); }
constexpr int64_t END = 3000000000;
struct Options
{
    std::string name;
    TaskProfile profile{TaskProfile::DENSE_IMAGE};
    uint32_t source{1};
    int64_t faultAfter{400000000}, failFlowAfter{-1}, sourceF3After{-1}, holderF3After{-1};
    bool storageReject{}, pathReject{}, remoteBusy{}, fastPrimary{}, success{true}, lateSourceF3{};
    bool acceptUnregistered{}, checkRegistered{};
};
struct Driver
{
    Ptr<TaskCoordinator> tasks;
    OnlineTopologyController& topology;
    CheckpointManager& checkpoints;
    InputStagingManager& input;
    Ptr<FaultController> faults;
    const Options& options;
    uint64_t faultId{1};
    void AtFault(uint32_t node, int64_t delay, bool permanent)
    {
        if (delay < 0) return;
        const auto fire = [this, node, permanent] {
            FaultDefinition f;
            f.faultId = faultId++; f.nodeId = node; f.startTimeNs = Simulator::Now().GetNanoSeconds();
            f.faultType = permanent ? FaultType::SATELLITE : FaultType::COMPUTE;
            if (!permanent) { f.durationNs = 200000000; f.failureProbability = .2; f.f1Occurred = true; }
            std::vector<GeneratedFaultEvent> batch{{FaultEventType::START, f}};
            if (!permanent && node == 3 && options.holderF3After == options.faultAfter)
            {
                auto holder = f; holder.faultId = faultId++; holder.nodeId = 0;
                holder.faultType = FaultType::SATELLITE; holder.durationNs.reset();
                holder.failureProbability.reset(); holder.f1Occurred = false;
                batch.push_back({FaultEventType::START, holder});
            }
            faults->SubmitGeneratedBatch(batch);
        };
        if (permanent && node == options.source && options.lateSourceF3)
            Simulator::Schedule(NanoSeconds(delay-1), [fire] { Simulator::Schedule(NanoSeconds(1), fire); });
        else Simulator::Schedule(NanoSeconds(delay), fire);
    }
    void OnTask(const TaskEventRecord& e)
    {
        if (e.toState == TASK_RUNNING)
        {
            checkpoints.Execute({{e.taskId,0}, e.nodeId, e.simulationTimeNs},
                {ActionKind::START_CHECKPOINT, CheckpointConfiguration{50,4,2,0}});
            Check(checkpoints.Inventory(e.taskId)->active, "checkpoint initialization rejected");
            if (options.pathReject) topology.ApplyCommunicationFaultOverlay({options.source}, false);
            input.Request(tasks->GetTaskRuntimes().front().definition, 0);
            if (options.pathReject) topology.ApplyCommunicationFaultOverlay({}, false);
            const auto& task = tasks->GetTaskRuntimes().front().definition;
            if (!options.storageReject && !options.pathReject)
            {
                const auto& pending = input.Records().at(e.taskId);
                Check(pending.state == OptionalInputState::REQUESTED && !pending.flow,
                      "fixture missed unregistered request");
                const auto before = checkpoints.Pool(0).Reserved();
                const auto dependency = input.Resolve(task, 0);
                Check(checkpoints.Pool(0).Reserved() == before && !pending.closed,
                      "pending candidate query mutated owner");
                if (options.source == 0)
                    Check(dependency.mode == InputDependencyMode::IN_FLIGHT &&
                          !dependency.flowId && dependency.remainingNs == 1,
                          "pending LocalDelivery lost its real local event");
                else
                {
                    const auto fresh = tasks->GetTransferEngine()->EstimateAdmissiblePath(
                        task.sourceNodeId, 0).TransferTimeNs(task.inputBytes);
                    Check(dependency.mode == InputDependencyMode::FETCH &&
                          dependency.remainingNs == fresh && dependency.remainingNs.value_or(0) > 1 &&
                          dependency.diagnostic == "PREFETCH_NOT_ESTABLISHED" &&
                          dependency.refetchReason.empty(), "unregistered INPUT fabricated readiness");
                    if (options.acceptUnregistered)
                    {
                        // Owner-port contract: a final target has accepted FETCH before
                        // dispatcher registration. The queued guard must not resurrect it.
                        input.Accept(e.taskId, dependency, [](bool, uint64_t) {
                            Check(false, "FETCH invoked an adoption callback");
                        });
                        Check(pending.closed && !pending.failed && !pending.flow,
                              "pending request not cancelled at acceptance");
                    }
                }
            }
            if (options.checkRegistered)
                Simulator::Schedule(NanoSeconds(1), [this] {
                    const auto& pending = input.Records().at(1);
                    Check(pending.state == OptionalInputState::REQUESTED && pending.flow &&
                          pending.startedNs < 0, "fixture missed registered-before-admission boundary");
                    const auto& task = tasks->GetTaskRuntimes().front().definition;
                    const auto dependency = input.Resolve(task, 0);
                    Check(dependency.mode == InputDependencyMode::FETCH &&
                          dependency.remainingNs == tasks->GetTransferEngine()->EstimateAdmissiblePath(
                              task.sourceNodeId, 0).TransferTimeNs(task.inputBytes) &&
                          dependency.refetchReason.empty() &&
                          dependency.diagnostic == "PREFETCH_NOT_ESTABLISHED" && !pending.closed,
                          "registered ID mistaken for admitted flow");
                });
            AtFault(3, options.faultAfter, false);
            AtFault(options.source, options.sourceF3After, true);
            if (options.holderF3After != options.faultAfter) AtFault(0, options.holderF3After, true);
            if (options.remoteBusy)
                Simulator::Schedule(NanoSeconds(options.faultAfter-1), [this] {
                    for (auto service : tasks->GetComputeServices())
                        if (service->GetNodeId() == 0) Check(service->ReserveRecovery(99,1), "busy fixture");
                });
            if (options.failFlowAfter >= 0)
                Simulator::Schedule(NanoSeconds(options.failFlowAfter), [this] {
                    const auto& record = input.Records().at(1);
                    Check(record.flow && tasks->GetTransferEngine()->FinalizeTransferIfActive(record.flow,
                        TransferTerminalState::FAILED, TransferTerminalReason::TASK_FAILED), "missed live flow");
                });
            // Query repeatedly without adoption or allocations; includes active self-reservation.
            Simulator::Schedule(NanoSeconds(10000000), [this] {
                const auto& task = tasks->GetTaskRuntimes().front().definition;
                const auto a = input.Resolve(task, 0);
                const auto before = checkpoints.Pool(0).Used()+checkpoints.Pool(0).Reserved();
                const auto b = input.Resolve(task, 0);
                Check(a.mode == b.mode && a.flowId == b.flowId && a.remainingNs == b.remainingNs,
                      "resolver is not repeatable");
                Check(before == checkpoints.Pool(0).Used()+checkpoints.Pool(0).Reserved(), "resolver allocated");
                if (a.mode == InputDependencyMode::IN_FLIGHT)
                    Check(a.flowId && a.remainingNs && a.remainingNs ==
                          tasks->GetTransferEngine()->EstimateRemainingReceiverTimeNs(a.flowId),
                          "active flow did not use its causal receiver estimate");
            });
        }
        if (e.toState == TASK_RESULT_TRANSFERRING && e.fromState == TASK_RUNNING)
        { input.Release(e.taskId); checkpoints.OnTaskComputeComplete({e.taskId,0}); }
        if (IsTerminalTaskState(e.toState))
        { input.Release(e.taskId); checkpoints.OnTaskTerminal(e.taskId); }
    }
};

int64_t Run(const Options& o, const std::filesystem::path& output)
{
    auto config = satcompute::test::MakeOnlineTestConfig(2,8,"fixed",END,END,6171353);
    config.parameters.fixedDelaySeconds = .001;
    config.parameters.islBandwidthBps = 10000000000ULL;
    config.parameters.routingMode = "global-capacity-aware-hrw";
    OnlineTopologyController topology(config.parameters,config.constellation); topology.Initialize();
    auto tasks = CreateObject<TaskCoordinator>();
    ComputeProfile profile{{{0,100000},{2,100000},{3,o.fastPrimary ? 100000000ULL : 100000ULL},{4,100000}}};
    TaskTrace trace{{{1,o.source,3,4,52428800,4,100000,1,1,2,o.profile}}};
    if (o.profile == TaskProfile::LLM) trace.tasks.front().inputBytes = 400;
    else trace.tasks.front().computeWorkUnits = 78644;
    tasks->Initialize(profile,trace,topology,"size-aware",1024,config.parameters.islMtuBytes,
        config.parameters.receiverRcvBufBytes,false,END,2.0);
    auto fault = CreateObject<FaultController>();
    std::vector<uint32_t> ids; for (uint32_t i=0;i<16;++i) ids.push_back(i);
    fault->ConfigureGeneration(ids,END); fault->BindTopology(topology); fault->BindTaskCoordinator(tasks);
    CheckpointManager checkpoints(tasks,topology,2000000000,END,InputStagingPolicy::DEFERRED);
    InputStagingManager input(tasks,checkpoints,END,[&](auto node) {
        return o.storageReject ? uint64_t{0} : checkpoints.Pool(node).Free(); });
    Driver driver{tasks,topology,checkpoints,input,fault,o};
    tasks->ConnectTaskObserver(MakeCallback(&Driver::OnTask,&driver));
    FixedProtectionPolicy policy(50,4);
    RecoveryController recovery(tasks,topology,checkpoints,END,policy,RemoteBusyRecoveryPolicy::RELOCATE);
    recovery.SetInputDependencyResolver(&input);
    Simulator::Stop(NanoSeconds(END)); Simulator::Run();
    recovery.Finalize(); input.Finalize(); checkpoints.Finalize();
    for (auto service : tasks->GetComputeServices()) service->CancelRecovery(99,1);
    std::filesystem::create_directories(output/o.name);
    recovery.WriteMetrics(output/o.name); input.WriteMetrics(output/o.name);
    WriteProtectionMetrics(checkpoints,*tasks->GetTransferEngine(),output/o.name);
    const auto& record = input.Records().at(1);
    Check(tasks->GetTaskRuntimes().front().TaskSucceeded() == o.success, o.name+": task outcome");
    if (o.storageReject || o.pathReject)
        Check(record.state == OptionalInputState::ABSENT && !record.flow && record.startedNs < 0,
              "optional rejection did not stay ABSENT");
    if (o.remoteBusy) Check(record.wrongTarget && record.refetchReason == "WRONG_TARGET_REFETCH", "wrong-target classification");
    if (o.failFlowAfter >= 0 && o.failFlowAfter < o.faultAfter)
        Check(record.refetchReason == "FAILED_PREFETCH_REFETCH", "failed-prefetch classification");
    if (record.usedNs >= 0)
    {
        Check(record.handedOff && record.usedNs >= record.readyNs, "USED without actual readiness");
        const auto r = recovery.Summaries().front();
        Check(record.usedNs == r.computeStartedNs, "USED before actual compute start");
        Check(r.inputReceivedNs >= record.readyNs, "recovery synthesized receiver completion");
        if (r.inputMode == "PREFETCH_IN_FLIGHT")
            Check(record.sentAtFault < tasks->GetTransferEngine()->GetSentBytes(record.flow), "no post-fault flow continuation");
    }
    if (o.name == "in-flight") Check(record.usedNs >= 0 && record.readyNs > record.faultNs, "IN_FLIGHT fixture missed");
    if (o.name == "ready-source-f3") Check(record.usedNs >= 0, "source F3 killed resident INPUT");
    if (o.name == "same-ns-source-ready-first") Check(record.usedNs >= 0, "causal ready-first INPUT lost");
    if (o.name == "same-ns-source-fault-first") Check(record.usedNs < 0 && record.failed, "fault-first flow resurrected");
    if (o.source == 0) Check(!record.flow && record.readyNs == record.requestedNs+1, "fake local network");
    for (const auto& [node,pool] : checkpoints.Pools())
        Check(!pool->Used() && !pool->Reserved(), "INPUT/checkpoint pool leak");
    uint64_t fetches=0, proactive=0;
    for (const auto& f : checkpoints.Flows())
    {
        fetches += f.key.kind == ProtectionTransferKind::RECOVERY_INPUT;
        proactive += f.key.kind == ProtectionTransferKind::PREFETCH_INPUT;
        if (f.key.kind == ProtectionTransferKind::PREFETCH_INPUT)
            Check(f.bytes == trace.tasks.front().inputBytes && f.work == 0, "INPUT polluted WU state");
    }
    Check(proactive <= 1 && fetches <= 1, "duplicate INPUT lifecycle");
    if (record.handedOff) Check(fetches == 0, "adopted INPUT was fetched again");
    if (o.acceptUnregistered || o.checkRegistered)
    {
        Check(!record.failed && record.refetchReason.empty() && !record.handedOff &&
              record.startedNs < 0 && record.reason == "PREFETCH_NOT_ESTABLISHED",
              "pending request mislabeled as failed refetch/adopted");
        Check(fetches == 1 && record.usedNs < 0, "pending request bypassed actual recovery fetch");
        Check(!record.flow || tasks->GetTransferEngine()->GetSentBytes(record.flow) == 0,
              "cancelled pending flow sent bytes");
        if (o.acceptUnregistered) Check(proactive == 0 && !record.flow, "guard registered cancelled request");
        else Check(proactive == 1 && record.flow, "registered pending fixture lost flow identity");
    }
    const auto capacity = tasks->GetTransferEngine()->CollectCapacityAwareSummary();
    Check(!capacity.activePathCountAtEnd && !capacity.totalReservedRateBpsAtEnd, "capacity leak");
    std::cout << o.name << ": " << ToString(record.state) << " used=" << record.usedNs
              << " refetch=" << record.refetchReason << '\n';
    tasks->DisconnectTaskObserver(MakeCallback(&Driver::OnTask,&driver));
    const auto readyAfter = record.readyNs - record.requestedNs;
    Simulator::Destroy(); Ipv4AddressGenerator::Reset(); Mac48Address::ResetAllocationIndex();
    return readyAfter;
}
}
int main(int argc,char** argv)
{
    try
    {
        std::string output="output/compfrr-input-admission/lifecycle";
        CommandLine command; command.AddValue("outputDir","Fixture outputs",output); command.Parse(argc,argv);
        PeakQuotaLedger quotas; quotas.Replace(1,0,100);
        Check(quotas.Accounted(0,{{1,120}},{},{{1,80}})==180,"INPUT consumed checkpoint quota");
        Check(quotas.Accounted(0,{{1,120}},1,{{1,80}})==120,"quota replacement duplicated INPUT");
        const auto readyAfter = Run({"ready"},output);
        auto o=Options{"in-flight"}; o.faultAfter=10000000; Run(o,output);
        o=Options{"pending-unregistered"}; o.acceptUnregistered=true; Run(o,output);
        o=Options{"pending-registered-same-ns"}; o.checkRegistered=true; o.faultAfter=0; Run(o,output);
        o=Options{"sparse"}; o.profile=TaskProfile::SPARSE_INFERENCE; Run(o,output);
        o=Options{"compression"}; o.profile=TaskProfile::COMPRESSION; Run(o,output);
        o=Options{"llm"}; o.profile=TaskProfile::LLM; Run(o,output);
        o=Options{"local"}; o.source=0; Run(o,output);
        o=Options{"storage-reject"}; o.storageReject=true; Run(o,output);
        o=Options{"path-reject"}; o.pathReject=true; Run(o,output);
        o=Options{"normal-before-ready"}; o.faultAfter=-1; o.fastPrimary=true; Run(o,output);
        o=Options{"normal-after-ready"}; o.faultAfter=-1; Run(o,output);
        o=Options{"failed-refetch"}; o.failFlowAfter=5000000; Run(o,output);
        o=Options{"wrong-target"}; o.remoteBusy=true; Run(o,output);
        o=Options{"ready-source-f3"}; o.sourceF3After=100000000; Run(o,output);
        o=Options{"source-f3-before-ready"}; o.sourceF3After=5000000; o.success=false; Run(o,output);
        o=Options{"holder-f3"}; o.holderF3After=100000000; Run(o,output);
        o=Options{"adopted-failure"}; o.faultAfter=10000000; o.failFlowAfter=20000000; o.success=false; Run(o,output);
        o=Options{"same-ns-compute-fault"}; o.faultAfter=readyAfter; Run(o,output);
        o=Options{"same-ns-source-fault-first"}; o.sourceF3After=readyAfter; o.success=false; Run(o,output);
        o=Options{"same-ns-source-ready-first"}; o.sourceF3After=readyAfter; o.lateSourceF3=true; Run(o,output);
        o=Options{"same-batch-primary-holder"}; o.faultAfter=readyAfter; o.holderF3After=readyAfter; Run(o,output);
        std::cout << "input-staging-runtime: PASS (" << checks << " checks)\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
