build_lib(
  LIBNAME satcompute
  SOURCE_FILES
    model/satcompute-version.cc
    model/scenario-config.cc
    model/sha256.cc
    routing/algorithm/capacity-aware-hrw-policy.cc
    routing/algorithm/global-first-policy.cc
    routing/algorithm/hash-per-flow-policy.cc
    routing/algorithm/hrw-per-flow-policy.cc
    routing/algorithm/size-aware-hrw-policy.cc
    routing/common/routing-mode.cc
    routing/ns3/satcompute-ipv4-global-routing.cc
    routing/ns3/satcompute-ipv4-global-routing-helper.cc
    routing/state/capacity-reservation-state.cc
    routing/state/flow-route-registry.cc
    routing/state/size-aware-load-state.cc
    topology/ipv4/satellite-ipv4-addressing.cc
    topology/link/satellite-link-state.cc
    topology/replay/replay-topology-controller.cc
    topology/satellite-id-map.cc
    topology/snapshot/snapshot-reader.cc
    topology/snapshot/snapshot-schedule.cc
    traffic/network-transfer-config.cc
  HEADER_FILES
    model/satcompute-version.h
    model/scenario-config.h
    model/sha256.h
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
    routing/ns3/satcompute-ipv4-global-routing.h
    routing/ns3/satcompute-ipv4-global-routing-helper.h
    routing/state/capacity-reservation-state.h
    routing/state/flow-route-registry.h
    routing/state/flow-route-state.h
    routing/state/size-aware-load-state.h
    routing/state/size-aware-load-view.h
    topology/ipv4/satellite-ipv4-addressing.h
    topology/link/satellite-link-state.h
    topology/replay/replay-topology-controller.h
    topology/satellite-endpoint-view.h
    topology/satellite-id-map.h
    topology/snapshot/snapshot-reader.h
    topology/snapshot/snapshot-schedule.h
    topology/snapshot/snapshot-types.h
    traffic/network-transfer-config.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libpoint-to-point}
    ${libcsma}
)

# The platform entry point is a project executable, not an ns-3 example.  It is
# therefore available when the global examples and tests switches are disabled.
build_exec(
  EXECNAME satcompute
  EXECNAME_PREFIX satcompute_app_
  SOURCE_FILES app/satcompute.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute
)

# Project-owned C++ checks are normal executables because SCP-SatComPlate does
# not enable ns-3's global test suite.
build_exec(
  EXECNAME satcompute-config-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/config-loader-test.cc
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
  EXECNAME satcompute-routing-compatibility-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/routing-compatibility-test.cc
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
