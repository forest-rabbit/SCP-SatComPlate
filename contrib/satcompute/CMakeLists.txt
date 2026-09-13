build_lib(
  LIBNAME satcompute
  SOURCE_FILES
    para.cc
    common/time-conversion.cc
    protection/common/protection-types.cc
    traffic/local-delivery.cc
    protection/common/task-state-adapter.cc
    protection/policy/baseline/checkbullet/cb-sat-policy.cc
    protection/policy/baseline/checkbullet/cb-sat-state.cc
    protection/policy/baseline/checkbullet/cb-sat-manager.cc
    protection/policy/baseline/checkbullet/cb-sat-recovery.cc
    protection/policy/baseline/checkbullet/cb-sat-config.cc
    protection/policy/baseline/checkbullet/cb-sat-controller.cc
    protection/policy/baseline/checkbullet/cb-sat-metrics.cc
    protection/storage/backup-storage-pool.cc
    protection/runtime/protection-runtime.cc
    protection/policy/fixed/fixed-protection-policy.cc
    protection/policy/compfrr/frequency/compfrr-frequency-policy.cc
    protection/policy/compfrr/frequency/frequency-decision-gate.cc
    protection/policy/baseline/first-feasible-placement/first-feasible-placement-policy.cc
    protection/policy/feasible-placement-pairs.cc
    protection/policy/baseline/least-recovery-load/least-recovery-load-placement-policy.cc
    protection/mechanism/checkpoint/checkpoint-progress.cc
    protection/mechanism/checkpoint/checkpoint-manager.cc
    protection/runtime/fixed-protection-controller.cc
    protection/runtime/frequency-protection-controller.cc
    protection/runtime/frequency-storage-estimator.cc
    protection/runtime/placement-load-ledger.cc
    metrics/core/placement-load-metrics.cc
    protection/runtime/recovery-controller.cc
    protection/runtime/recompute-controller.cc
    protection/runtime/one-plus-one-controller.cc
    protection/mechanism/replication/replica-manager.cc
    metrics/core/replica-metrics.cc
    metrics/core/recovery-metrics.cc
    metrics/core/frequency-metrics.cc
    metrics/core/protection-metrics.cc
    tools/validation/compfrr-shadow/compfrr-shadow-model.cc
    tools/validation/compfrr-shadow/compfrr-shadow-evaluator.cc
    tools/validation/compfrr-shadow/compfrr-shadow-recorder.cc
    fault/fault-para.cc
    fault/model/compute-failure-predictor.cc
    fault/model/compute-fault-combination.cc
    fault/model/f1-self-state-fault-model.cc
    fault/model/f2-radiation-fault-model.cc
    fault/model/f3-debris-fault-model.cc
    fault/parameter/fault-parameter-validator.cc
    fault/runtime/fault-controller.cc
    fault/runtime/fault-model-engine.cc
    fault/runtime/fault-prediction-engine.cc
    fault/runtime/fault-state.cc
    fault/trace/fault-definition.cc
    fault/trace/fault-trace.cc
    metrics/core/fault-metrics.cc
    metrics/core/flow-metrics.cc
    metrics/core/link-window.cc
    metrics/core/link-metrics-recorder.cc
    metrics/core/run-summary.cc
    metrics/core/task-metrics.cc
    metrics/core/transfer-metrics.cc
    metrics/diagnostics/failure-diagnostics.cc
    metrics/diagnostics/flow-drop-reason-diagnostics.cc
    metrics/routing/capacity-aware-metrics.cc
    metrics/routing/ecmp-metrics.cc
    metrics/routing/ecmp-route-recorder.cc
    metrics/routing/size-aware-metrics.cc
    metrics/metrics.cc
    routing/algorithm/capacity-aware-hrw-policy.cc
    routing/algorithm/global-first-policy.cc
    routing/algorithm/hash-per-flow-policy.cc
    routing/algorithm/hrw-per-flow-policy.cc
    routing/algorithm/size-aware-hrw-policy.cc
    routing/common/routing-mode.cc
    routing/routing-policy-factory.cc
    routing/ns3/satcompute-ipv4-global-routing.cc
    routing/ns3/satcompute-ipv4-global-routing-helper.cc
    routing/state/capacity-reservation-state.cc
    routing/state/flow-route-registry.cc
    routing/state/size-aware-load-state.cc
    task/compute-profile.cc
    task/compute-service.cc
    task/compute-task.cc
    task/task-coordinator.cc
    task/task-trace.cc
    topology/export/topology-slice-exporter.cc
    topology/orbit/constellation-definition.cc
    topology/ipv4/satellite-ipv4-addressing.cc
    topology/link/satellite-link-state.cc
    topology/online/circular-orbit-topology-policy.cc
    topology/online/online-topology-controller.cc
    topology/online/plus-grid-candidate.cc
    topology/orbit/online-orbit-constellation.cc
    topology/satellite-id-map.cc
    topology/satellite-topology.cc
    traffic/network-transfer-application.cc
    traffic/network-transfer-config.cc
    traffic/network-transfer-engine.cc
    traffic/network-transfer-receiver.cc
    traffic/network-transfer-records.cc
  HEADER_FILES
    para.h
    common/time-conversion.h
    protection/common/protection-types.h
    traffic/local-delivery.h
    protection/common/task-state-adapter.h
    protection/policy/baseline/checkbullet/cb-sat-policy.h
    protection/policy/baseline/checkbullet/cb-sat-state.h
    protection/policy/baseline/checkbullet/cb-sat-manager.h
    protection/policy/baseline/checkbullet/cb-sat-recovery.h
    protection/policy/baseline/checkbullet/cb-sat-config.h
    protection/policy/baseline/checkbullet/cb-sat-controller.h
    protection/storage/backup-storage-pool.h
    protection/runtime/protection-runtime.h
    protection/policy/fixed/fixed-protection-policy.h
    protection/policy/compfrr/frequency/compfrr-frequency-policy.h
    protection/policy/compfrr/frequency/frequency-decision-gate.h
    protection/policy/placement-policy.h
    protection/policy/recovery-policy.h
    protection/runtime/decision-path-snapshot.h
    protection/policy/baseline/first-feasible-placement/first-feasible-placement-policy.h
    protection/policy/baseline/least-recovery-load/least-recovery-load-placement-policy.h
    protection/policy/baseline/fa-first-feasible-placement/fa-first-feasible-placement-policy.h
    protection/policy/baseline/fa-least-recovery-load/fa-least-recovery-load-placement-policy.h
    protection/mechanism/checkpoint/checkpoint-progress.h
    protection/mechanism/checkpoint/checkpoint-manager.h
    protection/runtime/fixed-protection-controller.h
    protection/runtime/frequency-protection-controller.h
    protection/runtime/frequency-storage-estimator.h
    protection/runtime/placement-load-ledger.h
    protection/runtime/recovery-controller.h
    protection/runtime/recompute-controller.h
    protection/runtime/one-plus-one-controller.h
    protection/mechanism/replication/replica-manager.h
    protection/policy/baseline/one-plus-one/one-plus-one-policy.h
    protection/policy/baseline/recompute/recompute-policy.h
    protection/runtime/protection-transfer-key.h
    metrics/core/protection-metrics.h
    tools/validation/compfrr-shadow/compfrr-shadow-model.h
    tools/validation/compfrr-shadow/compfrr-shadow-task-state.h
    tools/validation/compfrr-shadow/compfrr-shadow-evaluator.h
    tools/validation/compfrr-shadow/compfrr-shadow-recorder.h
    fault/fault-para.h
    fault/model/compute-failure-predictor.h
    fault/model/compute-fault-combination.h
    fault/model/f1-self-state-fault-model.h
    fault/model/f2-radiation-fault-model.h
    fault/model/f3-debris-fault-model.h
    fault/parameter/fault-parameter-validator.h
    fault/runtime/fault-controller.h
    fault/runtime/compute-failure-probability-record.h
    fault/runtime/fault-model-engine.h
    fault/runtime/fault-prediction-engine.h
    fault/runtime/fault-state.h
    fault/trace/fault-definition.h
    fault/trace/fault-trace.h
    metrics/core/fault-metrics.h
    metrics/core/flow-metrics.h
    metrics/core/link-window.h
    metrics/core/link-metrics-recorder.h
    metrics/core/run-summary.h
    metrics/core/task-metrics.h
    metrics/core/transfer-metrics.h
    metrics/diagnostics/failure-diagnostics.h
    metrics/diagnostics/flow-drop-reason-diagnostics.h
    metrics/routing/capacity-aware-metrics.h
    metrics/routing/ecmp-metrics.h
    metrics/routing/ecmp-route-recorder.h
    metrics/routing/size-aware-metrics.h
    metrics/metrics.h
    routing/algorithm/capacity-aware-hrw-policy.h
    routing/algorithm/capacity-aware-path-types.h
    routing/algorithm/capacity-aware-path-view.h
    routing/algorithm/global-first-policy.h
    routing/algorithm/hash-per-flow-policy.h
    routing/algorithm/hrw-per-flow-policy.h
    routing/algorithm/next-hop-policy.h
    routing/algorithm/path-policy.h
    routing/algorithm/size-aware-hrw-policy.h
    routing/common/ecmp-flow-key.h
    routing/common/ecmp-route-candidate.h
    routing/common/fnv1a64.h
    routing/common/routing-mode.h
    routing/routing-policy-factory.h
    routing/ns3/satcompute-ipv4-global-routing.h
    routing/ns3/satcompute-ipv4-global-routing-helper.h
    routing/state/capacity-reservation-state.h
    routing/state/flow-route-registry.h
    routing/state/flow-route-state.h
    routing/state/size-aware-load-state.h
    routing/state/size-aware-load-view.h
    task/compute-profile.h
    task/compute-service.h
    task/compute-task.h
    task/task-coordinator.h
    task/task-trace.h
    topology/export/topology-slice-exporter.h
    topology/orbit/constellation-definition.h
    topology/ipv4/satellite-ipv4-addressing.h
    topology/link/satellite-link-state.h
    topology/link/satellite-link.h
    topology/online/circular-orbit-topology-policy.h
    topology/online/online-topology-controller.h
    topology/online/plus-grid-candidate.h
    topology/orbit/online-orbit-constellation.h
    topology/satellite-endpoint-view.h
    topology/satellite-id-map.h
    topology/satellite-runtime-view.h
    topology/satellite-topology.h
    topology/satellite-topology-controller.h
    traffic/network-transfer-application.h
    traffic/network-transfer-config.h
    traffic/network-transfer-engine.h
    traffic/network-transfer-receiver.h
    traffic/network-transfer-records.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libflow-monitor}
    ${libmobility}
    ${libpoint-to-point}
    ${libcsma}
)

# SatCompute vendors nlohmann/json once at the repository root.  PUBLIC lets
# the platform executable and project-only tests include the same header.
target_include_directories(
  satcompute
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third-party>
)
set_source_files_properties(
  protection/policy/baseline/checkbullet/cb-sat-config.cc
  PROPERTIES COMPILE_DEFINITIONS
    "SATCOMPUTE_CB_MTBF_PROFILE=\"${CMAKE_CURRENT_SOURCE_DIR}/protection/policy/baseline/checkbullet/calibration/frozen-mtbf-profile.json\""
)

# The platform entry point is a project executable, not an ns-3 example.  It is
# therefore available when the global examples and tests switches are disabled.
build_exec(
  EXECNAME satcompute
  EXECNAME_PREFIX satcompute_app_
  SOURCE_FILES satcompute.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute
)

# Pure-model calibration tool. It does not run the network simulator or create
# an ns-3 example target.
build_exec(
  EXECNAME satcompute-f1-calibration
  EXECNAME_PREFIX satcompute_tool_
  SOURCE_FILES tools/validation/f1-calibration.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tools
)

# Orbit-only F2 exposure calibration. It creates native mobility nodes but no
# network stack, routing, task, fault-execution, or ns-3 example target.
build_exec(
  EXECNAME satcompute-f2-exposure-calibration
  EXECNAME_PREFIX satcompute_tool_
  SOURCE_FILES tools/validation/f2-exposure-calibration.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tools
)

# Long-horizon orbit-only F2 sampling and spatial-density evidence. It shares
# the native orbit and pure F2 model, but creates no network or task objects.
build_exec(
  EXECNAME satcompute-f2-spatial-validation
  EXECNAME_PREFIX satcompute_tool_
  SOURCE_FILES tools/validation/f2-spatial-validation.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tools
)
add_custom_target(
  satcompute-f2-spatial-validation
  DEPENDS satcompute_tool_satcompute-f2-spatial-validation
)

# Project-owned C++ checks are normal executables because SCP-SatComPlate does
# not enable ns-3's global test suite.
build_exec(
  EXECNAME satcompute-cb-sat-config-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/cb-sat-config-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-cb-sat-recovery-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/cb-sat-recovery-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-cb-sat-runtime-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/cb-sat-runtime-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-cb-sat-policy-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/cb-sat-policy-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-one-plus-one-baseline-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/one-plus-one-baseline-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-recompute-baseline-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/recompute-baseline-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-frequency-runtime-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/frequency-runtime-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-n5b-policy-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/n5b-policy-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-protection-contract-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/protection-contract-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-protection-path-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/protection-path-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-recovery-runtime-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/recovery-runtime-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-compfrr-shadow-model-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/compfrr-shadow-model-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-fault-risk-query-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/fault-risk-query-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-task-deadline-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/task-deadline-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-link-window-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/link-window-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-para-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/para-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-constellation-definition-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/constellation-definition-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-routing-policy-factory-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/routing-policy-factory-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-task-input-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/task-input-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-compute-service-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/compute-service-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-fault-lifecycle-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/fault-lifecycle-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-fault-trace-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/fault-trace-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-fault-model-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/fault-model-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-compute-fault-execution-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/compute-fault-execution-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-satellite-fault-execution-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/satellite-fault-execution-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-online-orbit-foundation-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/online-orbit-foundation-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-online-topology-controller-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/online-topology-controller-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)
