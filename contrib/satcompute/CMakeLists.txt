build_lib(
  LIBNAME satcompute
  SOURCE_FILES
    effective-config.cc
    para.cc
    resolved-config.cc
    model/satcompute-version.cc
    model/sha256.cc
    metrics/run-output-writer.cc
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
    topology/export/circular-orbit-trace-exporter.cc
    topology/orbit/constellation-definition.cc
    topology/ipv4/satellite-ipv4-addressing.cc
    topology/link/satellite-link-state.cc
    topology/online/circular-orbit-topology-policy.cc
    topology/online/online-topology-controller.cc
    topology/online/plus-grid-candidate.cc
    topology/orbit/online-orbit-constellation.cc
    topology/replay/replay-topology-controller.cc
    topology/satellite-id-map.cc
    topology/satellite-topology.cc
    topology/snapshot/snapshot-reader.cc
    topology/snapshot/snapshot-schedule.cc
    traffic/network-transfer-application.cc
    traffic/network-transfer-config.cc
    traffic/network-transfer-engine.cc
    traffic/network-transfer.cc
    traffic/network-transfer-receiver.cc
  HEADER_FILES
    effective-config.h
    para.h
    resolved-config.h
    model/satcompute-version.h
    model/sha256.h
    metrics/run-output-writer.h
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
    topology/export/circular-orbit-trace-exporter.h
    topology/orbit/constellation-definition.h
    topology/ipv4/satellite-ipv4-addressing.h
    topology/link/satellite-link-state.h
    topology/online/circular-orbit-topology-policy.h
    topology/online/online-topology-controller.h
    topology/online/plus-grid-candidate.h
    topology/orbit/online-orbit-constellation.h
    topology/replay/replay-topology-controller.h
    topology/satellite-endpoint-view.h
    topology/satellite-id-map.h
    topology/satellite-runtime-view.h
    topology/satellite-topology.h
    topology/satellite-topology-controller.h
    topology/snapshot/snapshot-reader.h
    topology/snapshot/snapshot-schedule.h
    topology/snapshot/snapshot-types.h
    traffic/network-transfer-application.h
    traffic/network-transfer-config.h
    traffic/network-transfer-engine.h
    traffic/network-transfer.h
    traffic/network-transfer-receiver.h
    traffic/network-transfer-records.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libmobility}
    ${libpoint-to-point}
    ${libcsma}
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

# Offline topology generation is a project tool that reuses the same orbit and
# policy library as the platform; it is not an ns-3 example.
build_exec(
  EXECNAME satcompute-topology-generator
  EXECNAME_PREFIX satcompute_tool_
  SOURCE_FILES tools/generation/topology/topology-generator.cc
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
  EXECNAME satcompute-resolved-config-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/resolved-config-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-effective-config-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/effective-config-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-input-contract-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/input-contract-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-snapshot-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/snapshot-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-link-state-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/link-state-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-replay-controller-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/replay-controller-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-satellite-topology-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/satellite-topology-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-routing-compatibility-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/routing-compatibility-test.cc
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
  EXECNAME satcompute-size-aware-routing-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/size-aware-routing-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-capacity-aware-routing-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/capacity-aware-routing-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-transfer-trace-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/transfer-trace-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-network-transfer-engine-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/network-transfer-engine-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-network-transfer-wrapper-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/network-transfer-wrapper-test.cc
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
  EXECNAME satcompute-task-coordinator-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/task-coordinator-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-run-output-writer-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/run-output-writer-test.cc
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

build_exec(
  EXECNAME satcompute-topology-trace-exporter-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/topology-trace-exporter-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-topology-replay-equivalence-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/topology-replay-equivalence-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)
