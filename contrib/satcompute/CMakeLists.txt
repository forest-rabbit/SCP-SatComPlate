build_lib(
  LIBNAME satcompute
  SOURCE_FILES
    para.cc
    common/time-conversion.cc
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
    fault/fault-para.h
    fault/model/compute-failure-predictor.h
    fault/model/compute-fault-combination.h
    fault/model/f1-self-state-fault-model.h
    fault/model/f2-radiation-fault-model.h
    fault/model/f3-debris-fault-model.h
    fault/parameter/fault-parameter-validator.h
    fault/runtime/fault-controller.h
    fault/runtime/fault-model-engine.h
    fault/runtime/fault-prediction-engine.h
    fault/runtime/fault-state.h
    fault/trace/fault-definition.h
    fault/trace/fault-trace.h
    metrics/core/fault-metrics.h
    metrics/core/flow-metrics.h
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

# Project-owned C++ checks are normal executables because SCP-SatComPlate does
# not enable ns-3's global test suite.
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
